#pragma once

#include "Arduino.h"

#include "esp_https_ota.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"

#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

static inline bool needUpdate(esp_app_desc_t *new_app_info) {
    if (new_app_info == NULL) {
        return false;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    esp_ota_get_partition_description(running, &running_app_info);

    // Compare the app versions: if equal no update is needed
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        return false;
    }

    return true;
}

inline void checkForOTA(const char *firmware_upgrade_url, int ota_recv_timeout, const char *ota_server_pem_start, bool skip_cert_common_name_check = false) {
    esp_http_client_config_t config;
    // Initialize config with null values to prevent undefined behaviour!
    memset(&config, 0, sizeof(config));

    config.url = firmware_upgrade_url,
    config.cert_pem = ota_server_pem_start,
    //config.use_global_ca_store = true;
    config.timeout_ms = ota_recv_timeout,
    config.skip_cert_common_name_check = skip_cert_common_name_check;
    config.buffer_size = 128;
    config.buffer_size_tx = 128;

    esp_https_ota_config_t ota_config;
    // Initialize config with null values to prevent undefined behaviour!
    memset(&ota_config, 0, sizeof(ota_config));
    ota_config.http_config = &config;

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        Serial.println("ESP HTTPS OTA Begin failed");
        return;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        Serial.println("esp_https_ota_read_img_desc failed");
        goto ota_end;
    }
    if (!needUpdate(&app_desc)) {
        err = ESP_FAIL;
        Serial.println("ESP_HTTPS_OTA no update needed!");
        goto ota_end;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        Serial.print("OTA Image bytes read: ");
        Serial.println(esp_https_ota_get_image_len_read(https_ota_handle));
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        Serial.println("Complete data was not received.");
    }

ota_end:
    esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
    if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
        Serial.println("ESP_HTTPS_OTA upgrade successful. Rebooting ...");
        esp_restart();
    } else {
        if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
            Serial.println("Image validation failed, image is corrupted");
        }
        Serial.print("ESP_HTTPS_OTA upgrade failed ");
        Serial.println(ota_finish_err);
        Serial.print("ESP_HTTPS_OTA err: ");
        Serial.println(err);
    }
}

inline void initOTA() {
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
}
