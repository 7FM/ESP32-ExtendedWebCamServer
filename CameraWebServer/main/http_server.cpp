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
#include "Arduino.h"
#include "avi_helper.hpp"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"

//FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>

//Timer & Interrupts
#include "driver/timer.h"

// Local files
#include "camera_website_index.h"
#include "fs_helper.h"
#include "http_server.h"

// Include the config
#include "sdkconfig.h"

#ifdef CONFIG_OTA_FEATURE
#define OTA_FEATURE
#endif

#ifdef OTA_FEATURE
extern void checkForUpdate();

static inline int handleUpdateCheck() {
    checkForUpdate();
    return 0;
}
#endif

#define TIMER_GROUP_NUM(x) (x == 0 ? TIMER_GROUP_0 : TIMER_GROUP_1)
#define TIMER_NUM(x) (x == 0 ? TIMER_0 : TIMER_1)

#ifdef CONFIG_CAM_TASK_TIMER_GROUP_NUM
#define CAM_TASK_TIMER_GROUP_NUM CONFIG_CAM_TASK_TIMER_GROUP_NUM
#endif
#ifdef CONFIG_CAM_TASK_TIMER_NUM
#define CAM_TASK_TIMER_NUM CONFIG_CAM_TASK_TIMER_NUM
#endif
#ifdef CONFIG_TMP_INDEX_FILE_PATH
#define TMP_INDEX_FILE_PATH CONFIG_TMP_INDEX_FILE_PATH
#endif

#ifndef CAM_TASK_TIMER_GROUP_NUM
#define CAM_TASK_TIMER_GROUP_NUM 1
#endif
#ifndef CAM_TASK_TIMER_NUM
#define CAM_TASK_TIMER_NUM 1
#endif
#ifndef TMP_INDEX_FILE_PATH
#define TMP_INDEX_FILE_PATH ".tmpavi.idx"
#endif

#ifdef CONFIG_FLASH_LED_PIN
#define FLASH_LED_PIN CONFIG_FLASH_LED_PIN
#endif

#ifndef FLASH_LED_PIN
#define FLASH_LED_PIN 4
#endif

#define _CAM_TASK_TIMER_GROUP_NUM TIMER_GROUP_NUM(CAM_TASK_TIMER_GROUP_NUM)
#define _CAM_TASK_TIMER_NUM TIMER_NUM(CAM_TASK_TIMER_NUM)

#ifndef TIMER_DIVIDER
#define TIMER_DIVIDER 65536 //Range is 2 to 65536
#endif

#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds

#define BOOL_TO_STR(x) (x ? "true" : "false")
#define CONST_STR_LEN(x) (sizeof(x) / sizeof((x)[0]) - 1)
#define MAX(x, y) (x >= y ? x : y)

#define PART_BOUNDARY "123456789000000000000987654321"
#define _STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" PART_BOUNDARY
#define _STREAM_BOUNDARY "\r\n--" PART_BOUNDARY "\r\n"
#define _STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

#define JPG_QUALITY 80
#define FLASH_LIGHT_WAIT 20 / portTICK_PERIOD_MS

static httpd_handle_t stream_httpd = NULL;
static httpd_handle_t camera_httpd = NULL;

extern bool SDCardAvailable;

static int camLEDStatus = 0;
static int useFlash = 0;
static bool lapseRunning = false;

static QueueHandle_t frameQueue = xQueueCreate(10, sizeof(camera_fb_t *));
static TaskHandle_t cameraTask;
static TaskHandle_t aviTask;

static size_t framesTaken = 0;
static size_t maxFrameBytes = 0;
static FILE *aviFile = NULL;
static FILE *indexFile = NULL;
static size_t aviWriteOffset;

// 2 FPS in the resulting video
static size_t videoFPS = 2;
// Take a picture every second
static size_t millisBetweenSnapshots = 1000;

static IRAM_ATTR void timerISR(void *arg) {
    // Take lock //TODO needed??
    //timer_spinlock_take(_CAM_TASK_TIMER_GROUP_NUM);

    // Clear the interrupt status and enable arlam again
    timer_group_clr_intr_status_in_isr(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM);
    timer_group_enable_alarm_in_isr(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM);

    // Release lock
    //TODO needed?? and if so where??
    //timer_spinlock_give(_CAM_TASK_TIMER_GROUP_NUM);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* Notify the task that the transmission is complete. */
    vTaskNotifyGiveFromISR(cameraTask, &xHigherPriorityTaskWoken);

    /* If xHigherPriorityTaskWoken is now set to pdTRUE then a context switch
    should be performed to ensure the interrupt returns directly to the highest
    priority task.  The macro used for this purpose is dependent on the port in
    use and may be called portEND_SWITCHING_ISR(). */
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void cameraTaskRoutine(void *arg) {
    // 20s max block time
    const TickType_t xMaxBlockTime = pdMS_TO_TICKS(20000);

    for (;;) {
        // Reset the notify count to zero after processing one frame
        if (ulTaskNotifyTake(pdTRUE, xMaxBlockTime)) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb) {
                Serial.println("Camera capture failed!");
                break;
            }
            if (xQueueSend(frameQueue, &fb, xMaxBlockTime >> 1) == pdFALSE) {
                ++framesTaken;
            } else {
                Serial.println("WARNING: frame queue is full!");
                // Return buffer and wait for next timer call
                esp_camera_fb_return(fb);
            }
        }
    }
}

static void aviTaskRoutine(void *arg) {
    // 20s max block time
    const TickType_t xMaxBlockTime = pdMS_TO_TICKS(20000);
    size_t *offset = &aviWriteOffset;

    for (;;) {
        camera_fb_t *fb;
        if (xQueueReceive(frameQueue, &fb, xMaxBlockTime)) {
            size_t _jpg_buf_len = 0;
            uint8_t *_jpg_buf = NULL;

            bool needsFree = false;

            if (fb->format != PIXFORMAT_JPEG) {
                needsFree = frame2jpg(fb, JPG_QUALITY, &_jpg_buf, &_jpg_buf_len);
                if (!needsFree) {
                    Serial.println("JPEG compression failed!");
                    goto return_fb;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }

            // Update max frame byte count
            maxFrameBytes = MAX(maxFrameBytes, _jpg_buf_len);

            // Write frame to avi file and create index file
            writeFrameAndUpdate(aviFile, indexFile, offset, (const char *)_jpg_buf, _jpg_buf_len);

            if (needsFree) {
                free(_jpg_buf);
            }

        return_fb:
            esp_camera_fb_return(fb);
        }
    }
}

static inline void startTimer() {

    timer_config_t config;

    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = TIMER_AUTORELOAD_EN;

    timer_init(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM, &config);

    // Set the start and reset counter value
    timer_set_counter_value(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM, 0);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM, (millisBetweenSnapshots * TIMER_SCALE) / 1000);
    timer_enable_intr(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM);
    timer_isr_register(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM, timerISR, NULL, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM);
}

static inline void stopTimer() {
    timer_pause(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM);
    timer_disable_intr(_CAM_TASK_TIMER_GROUP_NUM, _CAM_TASK_TIMER_NUM);
}

static inline int handleLapse(sensor_t *s, int lapse) {
    bool wantsLapseStart = lapse ? true : false;

    // If wanted state is already actual state we are out of state sync!
    if (lapseRunning == wantsLapseStart) {
        return 1;
    }

    if (lapseRunning) {
        stopTimer();
        // Suspend camera Task because it is no longer needed!
        vTaskSuspend(cameraTask);

        // Loop until queue is empty
        const TickType_t xDelay = 500 / portTICK_PERIOD_MS;
        while (uxQueueMessagesWaiting(frameQueue)) {
            vTaskDelay(xDelay);
        }

        // Send task suspend request
        vTaskSuspend(aviTask);

        // Wait until the task is really suspended!
        while (eTaskGetState(aviTask) != eSuspended) {
            vTaskDelay(xDelay);
        }

        mergeAndPatch(aviFile, indexFile, &aviWriteOffset, framesTaken, maxFrameBytes, videoFPS);

        lapseRunning = false;
        fclose(aviFile);
        fclose(indexFile);
        aviFile = NULL;
        indexFile = NULL;

        // Delete temporary file
        remove(TMP_INDEX_FILE_PATH);
    } else {
        size_t width;
        size_t height;

        switch (s->status.framesize) {
            case FRAMESIZE_QXGA:
                width = 2048;
                height = 1564;
                break;
            case FRAMESIZE_UXGA:
                width = 1600;
                height = 1200;
                break;
            case FRAMESIZE_SXGA:
                width = 1280;
                height = 1024;
                break;
            case FRAMESIZE_XGA:
                width = 1024;
                height = 768;
                break;
            case FRAMESIZE_SVGA:
                width = 800;
                height = 600;
                break;
            case FRAMESIZE_VGA:
                width = 640;
                height = 480;
                break;
            case FRAMESIZE_CIF:
                width = 400;
                height = 296;
                break;
            case FRAMESIZE_QVGA:
                width = 320;
                height = 240;
                break;
            case FRAMESIZE_HQVGA:
                width = 240;
                height = 176;
                break;
            case FRAMESIZE_QQVGA:
                width = 160;
                height = 120;
                break;

            // Well there should be no other option!
            default:
                return 1;
        }

        //TODO proper file naming convensions... include date/time?
        aviFile = fopen("test.avi", "wb");
        indexFile = fopen(TMP_INDEX_FILE_PATH, "wb+");

        aviWriteOffset = createAVI_File(aviFile, width, height, videoFPS);

        framesTaken = 0;
        maxFrameBytes = 0;
        lapseRunning = true;

        vTaskResume(cameraTask);
        vTaskResume(aviTask);
        startTimer();
    }

    return 0;
}

static inline int handleLED(int led) {
    if (led == camLEDStatus) {
        return 1;
    }

    camLEDStatus = led;

    pinMode(FLASH_LED_PIN, OUTPUT);
    if (led) {
        digitalWrite(FLASH_LED_PIN, HIGH);
    } else {
        digitalWrite(FLASH_LED_PIN, LOW);
    }

    return 0;
}

static inline unsigned char h2int(char c) {
    if (c >= '0' && c <= '9') {
        return ((unsigned char)c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return ((unsigned char)c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return ((unsigned char)c - 'A' + 10);
    }
    return '\0';
}

static inline bool urldecode(char *str, size_t length) {
    size_t resIdx = 0;
    for (size_t i = 0; i < length; i++) {
        char c = str[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= length) {
                // Invalid encoding abort!
                str[resIdx] = '\0';
                return false;
            }
            char code0 = str[++i];
            char code1 = str[++i];
            c = (h2int(code0) << 4) | h2int(code1);
        }
        str[resIdx++] = c;
    }
    str[resIdx] = '\0';
    return true;
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
        pinMode(FLASH_LED_PIN, OUTPUT);
        digitalWrite(FLASH_LED_PIN, HIGH);
        vTaskDelay(FLASH_LIGHT_WAIT);
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (mustEnableFlashLight) {
        digitalWrite(FLASH_LED_PIN, LOW);
    }

    if (!fb) {
        Serial.println("Camera capture failed");
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

    do {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed!");
            res = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            bool jpeg_converted = frame2jpg(fb, JPG_QUALITY, &_jpg_buf, &_jpg_buf_len);
            if (!jpeg_converted) {
                Serial.println("JPEG compression failed!");
                res = ESP_FAIL;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, CONST_STR_LEN(_STREAM_BOUNDARY));

            if (res == ESP_OK) {
                size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);

                if (res == ESP_OK) {
                    res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
                }
            }
        }

        esp_camera_fb_return(fb);

        if (fb->format != PIXFORMAT_JPEG) {
            free(_jpg_buf);
            _jpg_buf = NULL;
            _jpg_buf_len = 0;
        }
    } while (res == ESP_OK);

    return res;
}

//TODO option to download recorded files
//TODO add feature to stop lapse automatically after certain time/number of frames
//TODO maybe EEPROM for camera parameters? add option to save/load and load and set at configure phase
static esp_err_t cmd_handler(httpd_req_t *req) {
    char variable[32];
    char value[32];

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = (char *)malloc(buf_len);
        if (!buf) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK &&
            httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
            httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
            free(buf);
        } else {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int val = atoi(value);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!lapseRunning && !strcmp(variable, "framesize")) {
        if (s->pixformat == PIXFORMAT_JPEG) {
            res = s->set_framesize(s, (framesize_t)val);
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

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    sensor_t *s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        return httpd_resp_send(req, (const char *)index_ov3660_html_gz,
                               index_ov3660_html_gz_len);
    }
    return httpd_resp_send(req, (const char *)index_ov2640_html_gz,
                           index_ov2640_html_gz_len);
}

static bool handleFSEntry(const char *name, bool isDir, void *arg) {
    httpd_req_t *req = (httpd_req_t *)arg;

#define NAME_FIELD ",{\"name\":\""
#define IS_DIR "\",\"is_dir\":"
#define IS_DIR_TRUE IS_DIR "true}"
#define IS_DIR_FALSE IS_DIR "false}"
    return httpd_resp_send_chunk(req, NAME_FIELD, CONST_STR_LEN(NAME_FIELD)) == ESP_OK &&
           httpd_resp_sendstr_chunk(req, name) == ESP_OK &&
           (isDir ? httpd_resp_send_chunk(req, IS_DIR_TRUE, CONST_STR_LEN(IS_DIR_TRUE)) : httpd_resp_send_chunk(req, IS_DIR_FALSE, CONST_STR_LEN(IS_DIR_FALSE))) == ESP_OK;
}

static esp_err_t filesystem_handler(httpd_req_t *req) {

    if (SDCardAvailable) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *buf;

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;

    //TODO adjustable?
    char filepath[256];

    if (buf_len > 1) {
        buf = (char *)malloc(buf_len);
        if (!buf) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) != ESP_OK ||
            httpd_query_key_value(buf, "path", filepath, sizeof(filepath)) != ESP_OK) {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    if (!urldecode(filepath, strlen(filepath))) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t res = ESP_OK;

    switch (getType(filepath)) {
        case DIR_TYPE: {
            // If this is a dir we want to browse that dir else download the file
            httpd_resp_set_type(req, "application/json");

#define PARENT_ENTRY "[{\"name\":\"..\",\"is_dir\":true}"
            if (httpd_resp_send_chunk(req, PARENT_ENTRY, CONST_STR_LEN(PARENT_ENTRY)) != ESP_OK || !listDirectory(filepath, handleFSEntry, req) || httpd_resp_send_chunk(req, "]", 1) != ESP_OK) {
                res = ESP_FAIL;
            }
            break;
        }

        case FILE_TYPE: {
            // Else download the file

            httpd_resp_set_type(req, "text/plain");

            FILE *file = fopen(filepath, "rb");

            if (!file) {
                res = ESP_FAIL;
            } else {
                char readBuf[512];
                for (int read; (read = fread(readBuf, 1, sizeof(readBuf), file));) {
                    if (httpd_resp_send_chunk(req, readBuf, read) != ESP_OK) {
                        res = ESP_FAIL;
                        break;
                    }
                }

                fclose(file);
            }
            break;
        }

        default:
            res = ESP_FAIL;
            break;
    }

    httpd_resp_send_chunk(req, NULL, 0);

    if (res != ESP_OK) {
        httpd_resp_send_500(req);
    }

    return res;
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri = {.uri = "/",
                             .method = HTTP_GET,
                             .handler = index_handler,
                             .user_ctx = NULL};

    httpd_uri_t status_uri = {.uri = "/status",
                              .method = HTTP_GET,
                              .handler = status_handler,
                              .user_ctx = NULL};

    httpd_uri_t cmd_uri = {.uri = "/control",
                           .method = HTTP_GET,
                           .handler = cmd_handler,
                           .user_ctx = NULL};

    httpd_uri_t capture_uri = {.uri = "/capture",
                               .method = HTTP_GET,
                               .handler = capture_handler,
                               .user_ctx = NULL};

    httpd_uri_t fs_uri = {.uri = "/fs",
                          .method = HTTP_GET,
                          .handler = filesystem_handler,
                          .user_ctx = NULL};

    httpd_uri_t stream_uri = {.uri = "/stream",
                              .method = HTTP_GET,
                              .handler = stream_handler,
                              .user_ctx = NULL};

    //TODO add favicon.ico
    config.stack_size = 7148;
    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &fs_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    config.stack_size = 2048;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }

    if (SDCardAvailable) {
        xTaskCreatePinnedToCore(
            cameraTaskRoutine,
            "CameraTask",
            2048,
            NULL,
            1,
            &cameraTask,
            tskNO_AFFINITY);

        xTaskCreatePinnedToCore(
            aviTaskRoutine,
            "AVI_Task",
            2048,
            NULL,
            2,
            &aviTask,
            tskNO_AFFINITY);

        vTaskSuspend(cameraTask);
        vTaskSuspend(aviTask);
    }

    //TODO check actually needed stack sizes: https://www.esp32.com/viewtopic.php?t=3692 https://www.freertos.org/uxTaskGetSystemState.html
}
