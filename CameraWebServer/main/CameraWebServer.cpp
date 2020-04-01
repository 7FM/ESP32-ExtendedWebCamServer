// Libraries
#include "Arduino.h"
#include "driver/sdmmc_host.h"
#include "esp_camera.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include <ESPmDNS.h>
#include <WiFi.h>

// Local files
#include "config_reader.hpp"
#include "http_server.hpp"
#include "makros.h"

// Include the config
#include "config.h"

#ifdef OTA_FEATURE

#include "ota_handler.hpp"

extern const uint8_t ota_server_ca_pem_start[] asm("_binary_ota_server_ca_pem_start");
//extern const uint8_t ota_server_pem_end[] asm("_binary_ota_server_ca_pem_end");
#endif

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

#ifdef OTA_FEATURE
#define MAX_OPTION_NAMES 7
#else
#define MAX_OPTION_NAMES 5
#endif

static const char *OPTION_NAMES[] = {
    //SSID
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
static String firmwareUpgradeURL = FALLBACK_FIRMWARE_UPGRADE_URL;

void checkForUpdate() {
    // Perform OTA update if available then reboot if successful
    checkForOTA(firmwareUpgradeURL.c_str(), OTA_RECV_TIMEOUT, (const char *)ota_server_ca_pem_start);
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

static inline void initWifi(const char *ssid, const char *password, const char *devName, bool tryForever = false) {

    // Disable wifi power safing
    esp_wifi_set_ps(WIFI_PS_NONE);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(devName);
    WiFi.begin(ssid, password);

    delay(1000);
    for (int tries = 0; WiFi.status() != WL_CONNECTED; ++tries) {

        if (tries == 10) {
            Serial.println("Cannot connect - try again");
            WiFi.begin(ssid, password);
        } else if (tries >= 20) {
            if (tryForever) {
                tries = 0;
            } else {
                return;
            }
        }

        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");

    if (!MDNS.begin(devName)) {
        Serial.println("Error setting up MDNS responder!");
    } else {
        Serial.printf("mDNS responder started '%s'\n", devName);
    }

    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect");
}

extern "C" void app_main() {
    initArduino();

    Serial.begin(115200);
    Serial.setDebugOutput(true);

    camera_config_t config;

    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_reset = CAM_PIN_RESET;
    config.pin_sscb_sda = CAM_PIN_SIOD;
    config.pin_sscb_scl = CAM_PIN_SIOC;

    config.pin_d0 = CAM_PIN_D0;
    config.pin_d1 = CAM_PIN_D1;
    config.pin_d2 = CAM_PIN_D2;
    config.pin_d3 = CAM_PIN_D3;
    config.pin_d4 = CAM_PIN_D4;
    config.pin_d5 = CAM_PIN_D5;
    config.pin_d6 = CAM_PIN_D6;
    config.pin_d7 = CAM_PIN_D7;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;

    config.xclk_freq_hz = 20000000;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pixel_format = PIXFORMAT_JPEG;
    // init with high specs to pre-allocate larger buffers
    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

#if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
#endif

    //power up the camera if PWDN pin is defined
    if (CAM_PIN_PWDN != -1) {
        pinMode(CAM_PIN_PWDN, OUTPUT);
        digitalWrite(CAM_PIN_PWDN, LOW);
    }

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get();
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);       // flip it back
        s->set_brightness(s, 1);  // up the blightness just a bit
        s->set_saturation(s, -2); // lower the saturation

        // Set highest resolution
        s->set_framesize(s, FRAMESIZE_QXGA);
    } else {
        // Set highest resolution
        s->set_framesize(s, FRAMESIZE_UXGA);
    }

    // No special effects
    s->set_special_effect(s, 0);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
#endif

    // Turn flash light off
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

    String ssid = FALLBACK_WIFI_SSID;
    String password = FALLBACK_WIFI_PWD;
    String devName = FALLBACK_DEVICE_NAME;
    String hMirror;
    String vFlip;
#ifdef OTA_FEATURE
    String otaCertPath;
#endif

    // initialize & mount SD card
    sdmmc_card_t *card;
    if (initSDcard(&card) != ESP_OK) {
        Serial.println("Card Mount Failed");
    } else {

        FILE *configFile = fopen(CONFIG_FILE_PATH, "r");

        // Read config file if it exists
        if (configFile) {
#ifdef OTA_FEATURE
            String *const parameters[] = {&ssid, &password, &devName, &hMirror, &vFlip, &firmwareUpgradeURL, &otaCertPath};
            readConfig(configFile, (const char *const *)OPTION_NAMES, NUMELEMS(OPTION_NAMES), OPTION_NAME_LENGTHS, parameters);
#else
            String *const parameters[] = {&ssid, &password, &devName, &hMirror, &vFlip};
            readConfig(configFile, (const char *const *)OPTION_NAMES, NUMELEMS(OPTION_NAMES), OPTION_NAME_LENGTHS, parameters);
#endif
            fclose(configFile);
        }

        SDCardAvailable = true;
    }

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

#ifdef OTA_FEATURE
    // Init ota
    initOTA();
#endif

    initWifi(ssid.c_str(), password.c_str(), devName.c_str(), true);

#ifdef OTA_FEATURE
    checkForUpdate();
#endif

    startCameraServer();

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {

            Serial.println("***** WiFi reconnect *****");
            WiFi.reconnect();
            delay(10000);

            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("***** WiFi restart *****");
                initWifi(ssid.c_str(), password.c_str(), devName.c_str());
            }
        }

        // Wait for 60s
        delay(60000);
    }
}