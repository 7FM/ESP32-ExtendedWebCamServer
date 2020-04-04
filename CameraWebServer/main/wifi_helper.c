/* ESPRESSIF MIT License
 * 
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 * 
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "esp_event_loop.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mdns.h"

#include "config.h"
#include "wifi_helper.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "camera wifi";
#endif

volatile int isWiFiSTAMode = 0;
volatile int isConnectedToWiFi = 0;

static int s_retry_num = 0;

static esp_err_t event_handler(void *ctx, system_event_t *event) {
    switch (event->event_id) {
        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "station:" MACSTR " join, AID=%d",
                     MAC2STR(event->event_info.sta_connected.mac),
                     event->event_info.sta_connected.aid);
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "station:" MACSTR "leave, AID=%d",
                     MAC2STR(event->event_info.sta_disconnected.mac),
                     event->event_info.sta_disconnected.aid);
            break;
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "got ip:%s",
                     ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            s_retry_num = 0;
            isConnectedToWiFi = 1;
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED: {
            if (s_retry_num < WIFI_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            } else {
                // Only set to zero if already tried to reconnect!
                // Now the only option is to initialize completely new!
                isConnectedToWiFi = 0;
            }
            ESP_LOGI(TAG, "connect to the AP fail");
            break;
        }
        default:
            break;
    }
    mdns_handle_system_event(ctx, event);
    return ESP_OK;
}

static void wifi_init_softap(const char *ap_ssid, const char *ap_pwd, const char *ap_ip_addr, const char *devName) {
    if (ap_ip_addr && ap_ip_addr[0] != '\0') {
        int a, b, c, d;
        sscanf(ap_ip_addr, "%d.%d.%d.%d", &a, &b, &c, &d);
        tcpip_adapter_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, a, b, c, d);
        IP4_ADDR(&ip_info.gw, a, b, c, d);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(WIFI_IF_AP));
        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(WIFI_IF_AP, &ip_info));
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(WIFI_IF_AP));
    }
    if (devName && devName[0] != '\0') {
        tcpip_adapter_set_hostname(WIFI_IF_AP, devName);
    }
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    snprintf((char *)wifi_config.ap.ssid, 32, "%s", ap_ssid);
    wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
    if (ap_pwd) {
        snprintf((char *)wifi_config.ap.password, 64, "%s", ap_pwd);
    }
    wifi_config.ap.max_connection = 1;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (!ap_pwd || ap_pwd[0] == '\0') {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
}

static void wifi_init_sta(const char *ssid, const char *pwd, const char *devName) {
    if (devName && devName[0] != '\0') {
        tcpip_adapter_set_hostname(WIFI_IF_STA, devName);
    }
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    snprintf((char *)wifi_config.sta.ssid, 32, "%s", ssid);
    if (pwd) {
        snprintf((char *)wifi_config.sta.password, 64, "%s", pwd);
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
}

int initWifi(const char *ssid, const char *pwd, const char *ap_ssid, const char *ap_pwd, const char *ap_ip_addr, const char *devName) {
    static int wasIntialized = 0;

    const int reinitialize = wasIntialized;

    if (reinitialize) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        wasIntialized = 0;
    }
    s_retry_num = 0;
    isConnectedToWiFi = 0;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_mode_t mode = WIFI_MODE_NULL;

    if (ap_ssid && ap_ssid[0] != '\0') {
        mode |= WIFI_MODE_AP;
        isWiFiSTAMode = 0;
    }
    if (ssid && ssid[0] != '\0') {
        mode |= WIFI_MODE_STA;
        isWiFiSTAMode = 1;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (mode == WIFI_MODE_NULL) {
        ESP_LOGE(TAG, "Neither AP or STA have been configured. WiFi will be off.");
        return -1;
    }

    tcpip_adapter_init();
    if (!reinitialize) {
        ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    }
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode & WIFI_MODE_AP) {
        wifi_init_softap(ap_ssid, ap_pwd, ap_ip_addr, devName);
    }

    if (mode & WIFI_MODE_STA) {
        wifi_init_sta(ssid, pwd, devName);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    wasIntialized = 1;
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    return 0;
}
