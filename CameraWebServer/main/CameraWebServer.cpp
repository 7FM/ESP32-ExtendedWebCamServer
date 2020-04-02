// Libraries
#include "driver/sdmmc_host.h"
#include "esp_camera.h"
#include "esp_vfs_fat.h"

// Include the config
#include "config.h"

// Local files
#include "WString.hpp"
#include "camera_helper.h"
#include "config_reader.hpp"
#include "http_server.hpp"
#include "makros.h"
#include "mdns_helper.h"
#include "wifi_helper.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "camera main";
#endif

#ifdef OTA_FEATURE

#include "ota_handler.h"

extern const uint8_t ota_server_ca_pem_start[] asm("_binary_ota_server_ca_pem_start");
//extern const uint8_t ota_server_pem_end[] asm("_binary_ota_server_ca_pem_end");

#define MAX_OPTION_NAMES 7

#else
#define MAX_OPTION_NAMES 5
#endif

static const char *OPTION_NAMES[] = {
    // SSID
    "SSID",
    "ssid",
    // Password
    "password",
    "Password",
    "Passwort",
    "pwd",
    // Devive Name
    "devname",
    "devName",
    "dev_name",
    "deviceName",
    "device_name",
    // AP SSID
    "AP_SSID",
    "ap_ssid",
    // AP Password
    "ap_password",
    "AP_Password",
    "AP_Passwort",
    "ap_pwd",
    // AP IP base address
    "ap_ipaddr",
    "ap_ip_addr",
    "AP_IP_addr",
    "AP_IP_ADDR",
    // Devive Name
    "devname",
    "devName",
    "dev_name",
    "deviceName",
    "device_name",
    // H-Mirror
    "H-Mirror",
    "H_Mirror",
    "h_mirror",
    "hMirror",
    // V-Flip
    "V-Flip",
    "V_Flip",
    "v_flip",
    "vFlip",
#ifdef OTA_FEATURE
    // Firmware Upgrade URL
    "firmwareUpgradeURL",
    "fwUpgradeURL",
    "fwURL",
    "fw_url",
    "firmware_url",
    "firmware_upgrade_url",
    "fw_upgrade_url",
    // OTA Server Cert Path
    "otaCertPath",
    "certPath",
    "ota_cert_path",
    "cert_path",
#endif
};

static int OPTION_NAME_LENGTHS[] = {
    2,
    4,
    2,
    4,
    4,
    5,
    4,
    4,
#ifdef OTA_FEATURE
    7,
    4,
#endif
};

bool SDCardAvailable = false;

#ifdef OTA_FEATURE
static String firmwareUpgradeURL(FALLBACK_FIRMWARE_UPGRADE_URL);

void checkForUpdate() {
    // Perform OTA update if available then reboot if successful
    checkForOTA(
        firmwareUpgradeURL.c_str(),
        OTA_RECV_TIMEOUT,
        (const char *)ota_server_ca_pem_start,
#ifdef SKIP_COMMON_NAME_CHECK
        true
#else
        false
#endif
    );
}
#endif

static inline esp_err_t initSDcard(sdmmc_card_t **card) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT; // using 1 bit mode
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1; // using 1 bit mode
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    return esp_vfs_fat_sdmmc_mount("", &host, &slot_config, &mount_config, card);
}

extern "C" void app_main() {
    if (initCamera()) {
        return;
    }

    String ssid(FALLBACK_WIFI_SSID);
    String pwd(FALLBACK_WIFI_PWD);
    String ap_ssid(FALLBACK_AP_SSID);
    String ap_pwd(FALLBACK_AP_PWD);
    String ap_ip_addr(FALLBACK_AP_IP_ADDR);
    String devName(FALLBACK_DEVICE_NAME);
    String hMirror;
    String vFlip;
#ifdef OTA_FEATURE
    String otaCertPath;
#endif

    // initialize & mount SD card
    sdmmc_card_t *card;
    if (initSDcard(&card) != ESP_OK) {
        ESP_LOGE(TAG, "Card Mount Failed");
        SDCardAvailable = false;
    } else {

#ifdef OTA_FEATURE
        String *const parameters[] = {&ssid, &pwd, &ap_ssid, &ap_pwd, &ap_ip_addr, &devName, &hMirror, &vFlip, &firmwareUpgradeURL, &otaCertPath};
        readConfig((const char *const *)OPTION_NAMES, NUMELEMS(OPTION_NAMES), OPTION_NAME_LENGTHS, parameters);
#else
        String *const parameters[] = {&ssid, &pwd, &ap_ssid, &ap_pwd, &ap_ip_addr, &devName, &hMirror, &vFlip};
        readConfig((const char *const *)OPTION_NAMES, NUMELEMS(OPTION_NAMES), OPTION_NAME_LENGTHS, parameters);
#endif
    }

    sensor_t *s = esp_camera_sensor_get();
    //TODO remove if eeprom solution is implemented
    if (!hMirror.isEmpty()) {
        const char *cstr = hMirror.c_str();
        if (!strcmp("true", cstr) || !strcmp("True", cstr)) {
            s->set_hmirror(s, 1);
        } else if (!strcmp("false", cstr) || !strcmp("False", cstr)) {
            s->set_hmirror(s, 0);
        }
    }
    if (!vFlip.isEmpty()) {
        const char *cstr = vFlip.c_str();
        if (!strcmp("true", cstr) || !strcmp("True", cstr)) {
            s->set_vflip(s, 1);
        } else if (!strcmp("false", cstr) || !strcmp("False", cstr)) {
            s->set_vflip(s, 0);
        }
    }

    if (initWifi(ssid.c_str(), pwd.c_str(), ap_ssid.c_str(), ap_pwd.c_str(), ap_ip_addr.c_str())) {
        return;
    }

#ifdef OTA_FEATURE
    //TODO wait until connected
    checkForUpdate();
#endif

    initMDNS(devName.c_str(), ssid.length());

    startCameraServer();

    //TODO reimplement reconnect
    /*
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {

            ESP_LOGI(TAG, "***** WiFi reconnect *****");
            WiFi.reconnect();
            delay(10000);

            if (WiFi.status() != WL_CONNECTED) {
                ESP_LOGI(TAG, "***** WiFi restart *****");
                initWifi(ssid.c_str(), password.c_str(), devName.c_str());
            }
        }

        // Wait for 60s
        delay(60000);
    }
    */
}