#pragma once

#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"
#include <stdio.h>

// Local files
#include "avi_helper.hpp"
#include "makros.h"

//FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

//Timer & Interrupts
#include "driver/timer.h"

// Include the config
#include "sdkconfig.h"

// Makros
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

#define _CAM_TASK_TIMER_GROUP_NUM TIMER_GROUP_NUM(CAM_TASK_TIMER_GROUP_NUM)
#define _CAM_TASK_TIMER_NUM TIMER_NUM(CAM_TASK_TIMER_NUM)

#ifndef TIMER_DIVIDER
#define TIMER_DIVIDER 65536 //Range is 2 to 65536
#endif

#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds

#define JPG_QUALITY 80

bool lapseRunning = false;
// 2 FPS in the resulting video
size_t videoFPS = 2;
// Take a picture every second
size_t millisBetweenSnapshots = 1000;

static QueueHandle_t frameQueue = xQueueCreate(10, sizeof(camera_fb_t *));
static TaskHandle_t cameraTask;
static TaskHandle_t aviTask;

static size_t framesTaken = 0;
static size_t maxFrameBytes = 0;
static FILE *aviFile = NULL;
static FILE *indexFile = NULL;
static size_t aviWriteOffset;

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
            maxFrameBytes = MAXEQ(maxFrameBytes, _jpg_buf_len);

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

void lapseHandlerSetup() {
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