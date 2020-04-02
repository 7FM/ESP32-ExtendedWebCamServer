// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "driver/ledc.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"

// Include the config
#include "config.h"

// Local files
#include "camera_helper.h"
#include "fs_browser.h"
#include "http_server.hpp"
#include "lapse_handler.hpp"
#include "makros.h"
#include "mdns_helper.h"
#include "web_utils.h"

#ifdef OTA_FEATURE
extern void checkForUpdate();

static inline int handleUpdateCheck() {
    checkForUpdate();
    return 0;
}
#endif

#define _PART_BOUNDARY "123456789000000000000987654321"
#define _STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" _PART_BOUNDARY
#define _STREAM_BOUNDARY "\r\n--" _PART_BOUNDARY "\r\n"
#define _STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "camera_httpd";
#endif

// External status variables
extern bool SDCardAvailable;
extern bool lapseRunning;
// 2 FPS in the resulting video
extern size_t videoFPS;
// Take a picture every second
extern size_t millisBetweenSnapshots;

// Local status variables
static int camLEDStatus = 0;
static int useFlash = 0;

static int led_duty = 255;
static bool isStreaming = false;

static inline void enable_led(bool en) { // Turn LED On or Off
    int duty = 0;

    if (en) {
        if (isStreaming && led_duty > CONFIG_LED_MAX_INTENSITY) {
            duty = CONFIG_LED_MAX_INTENSITY;
        } else {
            duty = led_duty;
        }
    }
    ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, (ledc_channel_t)CONFIG_LED_LEDC_CHANNEL, duty);
    ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, (ledc_channel_t)CONFIG_LED_LEDC_CHANNEL);
}

static inline int handleLED(int led) {
    if (led == camLEDStatus) {
        return 1;
    }

    camLEDStatus = led;

    if (led) {
        enable_led(true);
    } else {
        enable_led(false);
    }

    return 0;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
    httpd_req_t *req = (httpd_req_t *)arg;
    if (httpd_resp_send_chunk(req, (const char *)data, len) != ESP_OK) {
        return 0;
    }
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req) {
    bool mustEnableFlashLight = useFlash && !camLEDStatus;

    if (mustEnableFlashLight) {
        enable_led(true);
        vTaskDelay(FLASH_LIGHT_WAIT);
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (mustEnableFlashLight) {
        enable_led(false);
    }

    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t res;

    if (fb->format == PIXFORMAT_JPEG) {
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
        res = frame2jpg_cb(fb, JPG_QUALITY, jpg_encode_stream, req) ? ESP_OK : ESP_FAIL;
        httpd_resp_send_chunk(req, NULL, 0);
    }

    esp_camera_fb_return(fb);

    return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    // We do not allow streaming if a timelapse is running
    if (lapseRunning) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    //TODO LED?
    isStreaming = true;

    do {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        const bool notJPEG = fb->format != PIXFORMAT_JPEG;

        if (notJPEG) {
            bool jpeg_converted = frame2jpg(fb, JPG_QUALITY, &_jpg_buf, &_jpg_buf_len);
            if (!jpeg_converted) {
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, CONST_STR_LEN(_STREAM_BOUNDARY));

            if (res == ESP_OK) {
                size_t hlen = snprintf((char *)part_buf, sizeof(part_buf), _STREAM_PART, _jpg_buf_len);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);

                if (res == ESP_OK) {
                    res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
                }
            }
        }

        esp_camera_fb_return(fb);

        if (notJPEG) {
            free(_jpg_buf);
            _jpg_buf = NULL;
            _jpg_buf_len = 0;
        }
    } while (res == ESP_OK);

    isStreaming = false;

    return res;
}

//TODO add feature to stop lapse automatically after certain time/number of frames
//TODO maybe EEPROM for camera parameters? add option to save/load and load and set at configure phase
static esp_err_t cmd_handler(httpd_req_t *req) {
    char variable[32];
    char value[32];

    char *buf = NULL;
    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK ||
        httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int val = atoi(value);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!lapseRunning && !strcmp(variable, "framesize")) {
        if (s->pixformat == PIXFORMAT_JPEG) {
            res = s->set_framesize(s, (framesize_t)val);
            if (res == 0) {
                app_mdns_update_framesize(val);
            }
        }
    } else if (!strcmp(variable, "quality")) {
        res = s->set_quality(s, val);
    } else if (!strcmp(variable, "contrast")) {
        res = s->set_contrast(s, val);
    } else if (!strcmp(variable, "brightness")) {
        res = s->set_brightness(s, val);
    } else if (!strcmp(variable, "saturation")) {
        res = s->set_saturation(s, val);
    } else if (!strcmp(variable, "gainceiling")) {
        res = s->set_gainceiling(s, (gainceiling_t)val);
    } else if (!strcmp(variable, "colorbar")) {
        res = s->set_colorbar(s, val);
    } else if (!strcmp(variable, "awb")) {
        res = s->set_whitebal(s, val);
    } else if (!strcmp(variable, "agc")) {
        res = s->set_gain_ctrl(s, val);
    } else if (!strcmp(variable, "aec")) {
        res = s->set_exposure_ctrl(s, val);
    } else if (!strcmp(variable, "hmirror")) {
        res = s->set_hmirror(s, val);
    } else if (!strcmp(variable, "vflip")) {
        res = s->set_vflip(s, val);
    } else if (!strcmp(variable, "awb_gain")) {
        res = s->set_awb_gain(s, val);
    } else if (!strcmp(variable, "agc_gain")) {
        res = s->set_agc_gain(s, val);
    } else if (!strcmp(variable, "aec_value")) {
        res = s->set_aec_value(s, val);
    } else if (!strcmp(variable, "aec2")) {
        res = s->set_aec2(s, val);
    } else if (!strcmp(variable, "dcw")) {
        res = s->set_dcw(s, val);
    } else if (!strcmp(variable, "bpc")) {
        res = s->set_bpc(s, val);
    } else if (!strcmp(variable, "wpc")) {
        res = s->set_wpc(s, val);
    } else if (!strcmp(variable, "raw_gma")) {
        res = s->set_raw_gma(s, val);
    } else if (!strcmp(variable, "lenc")) {
        res = s->set_lenc(s, val);
    } else if (!strcmp(variable, "wb_mode")) {
        res = s->set_wb_mode(s, val);
    } else if (!strcmp(variable, "ae_level")) {
        res = s->set_ae_level(s, val);
    } else if (!strcmp(variable, "led_intensity")) {
        led_duty = val;
        if (camLEDStatus) {
            enable_led(true);
        }
    } else if (!strcmp(variable, "led")) {
        res = handleLED(val);
    } else if (!strcmp(variable, "use_flash")) {
        useFlash = val;
    } else if (!strcmp(variable, "lapse-running")) {
        if (SDCardAvailable) {
            res = handleLapse(s, val);
        }
    } else if (!lapseRunning && !strcmp(variable, "video_fps")) {
        if (val) {
            videoFPS = val;
        } else {
            res = -1;
        }
    } else if (!lapseRunning && !strcmp(variable, "frame_delay")) {
        if (val) {
            millisBetweenSnapshots = val;
        } else {
            res = -1;
        }
#ifdef OTA_FEATURE
    } else if (!strcmp(variable, "check-update")) {
        res = handleUpdateCheck();
#endif
    } else {
        res = -1;
    }

    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
    //TODO reduce size as needed!
    char json_response[512];

    sensor_t *s = esp_camera_sensor_get();
    char *p = json_response;
    *p++ = '{';

    p += sprintf(p, "\"board\":\"%s\",", CAM_BOARD);
    p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
    p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
    p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p += sprintf(p, "\"quality\":%u,", s->status.quality);
    p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p += sprintf(p, "\"awb\":%u,", s->status.awb);
    p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p += sprintf(p, "\"aec\":%u,", s->status.aec);
    p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p += sprintf(p, "\"agc\":%u,", s->status.agc);
    p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p += sprintf(p, "\"vflip\":%u,", s->status.vflip);
    p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p += sprintf(p, "\"colorbar\":%u,", s->status.colorbar);
    p += sprintf(p, "\"led\":%u,", camLEDStatus);
    p += sprintf(p, "\"use_flash\":%u,", useFlash);
    p += sprintf(p, "\"led_intensity\":%u,", led_duty);
    p += sprintf(p, "\"toggle-lapse\":%s,", BOOL_TO_STR(lapseRunning));
    p += sprintf(p, "\"sd-avail\":%s,", BOOL_TO_STR(SDCardAvailable));
    p += sprintf(p, "\"frame_delay\":%u,", millisBetweenSnapshots);
    p += sprintf(p, "\"video_fps\":%u,", videoFPS);
#ifdef OTA_FEATURE
    p += sprintf(p, "\"check-update\":true");
#else
    p += sprintf(p, "\"check-update\":false");
#endif
    *p++ = '}';
    //*p = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    int responseLength = p - json_response;
    return httpd_resp_send(req, json_response, responseLength);
}

static esp_err_t mdns_handler(httpd_req_t *req) {
    size_t json_len = 0;
    //TODO replace with malloc instead of static allocation or send chunks...
    const char *json_response = app_mdns_query(&json_len);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, json_len);
}

//TODO remove?
static esp_err_t xclk_handler(httpd_req_t *req) {
    char *buf = NULL;
    char _xclk[32];

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

    int xclk = atoi(_xclk);
    ESP_LOGI(TAG, "Set XCLK: %d MHz", xclk);

    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t win_handler(httpd_req_t *req) {
    char *buf = NULL;

    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }

    int startX = parse_get_var(buf, "sx", 0);
    int startY = parse_get_var(buf, "sy", 0);
    int endX = parse_get_var(buf, "ex", 0);
    int endY = parse_get_var(buf, "ey", 0);
    int offsetX = parse_get_var(buf, "offx", 0);
    int offsetY = parse_get_var(buf, "offy", 0);
    int totalX = parse_get_var(buf, "tx", 0);
    int totalY = parse_get_var(buf, "ty", 0);
    int outputX = parse_get_var(buf, "ox", 0);
    int outputY = parse_get_var(buf, "oy", 0);
    bool scale = parse_get_var(buf, "scale", 0) == 1;
    bool binning = parse_get_var(buf, "binning", 0) == 1;
    free(buf);

    ESP_LOGI(TAG, "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
    sensor_t *s = esp_camera_sensor_get();
    int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
    if (res) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        if (s->id.PID == OV3660_PID) {
            extern const unsigned char index_ov3660_html_gz_start[] asm("_binary_index_ov3660_html_gz_start");
            extern const unsigned char index_ov3660_html_gz_end[] asm("_binary_index_ov3660_html_gz_end");
            size_t index_ov3660_html_gz_len = index_ov3660_html_gz_end - index_ov3660_html_gz_start;
            return httpd_resp_send(req, (const char *)index_ov3660_html_gz_start, index_ov3660_html_gz_len);
        } else if (s->id.PID == OV5640_PID) {
            extern const unsigned char index_ov5640_html_gz_start[] asm("_binary_index_ov5640_html_gz_start");
            extern const unsigned char index_ov5640_html_gz_end[] asm("_binary_index_ov5640_html_gz_end");
            size_t index_ov5640_html_gz_len = index_ov5640_html_gz_end - index_ov5640_html_gz_start;
            return httpd_resp_send(req, (const char *)index_ov5640_html_gz_start, index_ov5640_html_gz_len);
        } else {
            extern const unsigned char index_ov2640_html_gz_start[] asm("_binary_index_ov2640_html_gz_start");
            extern const unsigned char index_ov2640_html_gz_end[] asm("_binary_index_ov2640_html_gz_end");
            size_t index_ov2640_html_gz_len = index_ov2640_html_gz_end - index_ov2640_html_gz_start;
            return httpd_resp_send(req, (const char *)index_ov2640_html_gz_start, index_ov2640_html_gz_len);
        }
    } else {
        ESP_LOGE(TAG, "Camera sensor not found");
        return httpd_resp_send_500(req);
    }
}

static esp_err_t monitor_handler(httpd_req_t *req) {
    extern const unsigned char monitor_html_gz_start[] asm("_binary_monitor_html_gz_start");
    extern const unsigned char monitor_html_gz_end[] asm("_binary_monitor_html_gz_end");
    size_t monitor_html_gz_len = monitor_html_gz_end - monitor_html_gz_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)monitor_html_gz_start, monitor_html_gz_len);
}

void startCameraServer() {

    httpd_handle_t stream_httpd = NULL;
    httpd_handle_t camera_httpd = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 9;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL};

    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    httpd_uri_t xclk_uri = {
        .uri = "/xclk",
        .method = HTTP_GET,
        .handler = xclk_handler,
        .user_ctx = NULL};

    httpd_uri_t win_uri = {
        .uri = "/resolution",
        .method = HTTP_GET,
        .handler = win_handler,
        .user_ctx = NULL};

    httpd_uri_t mdns_uri = {
        .uri = "/mdns",
        .method = HTTP_GET,
        .handler = mdns_handler,
        .user_ctx = NULL};

    httpd_uri_t monitor_uri = {
        .uri = "/monitor",
        .method = HTTP_GET,
        .handler = monitor_handler,
        .user_ctx = NULL};

    //TODO add favicon.ico
    config.stack_size = 7148;
    ESP_LOGI(TAG, "Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        if (SDCardAvailable) {
            registerFSHandler(camera_httpd);
        }

        httpd_register_uri_handler(camera_httpd, &xclk_uri);
        httpd_register_uri_handler(camera_httpd, &win_uri);

        httpd_register_uri_handler(camera_httpd, &mdns_uri);
        httpd_register_uri_handler(camera_httpd, &monitor_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    config.stack_size = 2048;
    config.max_uri_handlers = 1;
    ESP_LOGI(TAG, "Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }

    if (SDCardAvailable) {
        lapseHandlerSetup();
    }

    //TODO check actually needed stack sizes: https://www.esp32.com/viewtopic.php?t=3692 https://www.freertos.org/uxTaskGetSystemState.html
}
