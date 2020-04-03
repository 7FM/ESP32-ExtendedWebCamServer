#pragma once

#include "esp_camera.h"

void lapseHandlerSetup();
int handleLapse(sensor_t *s, int lapse);

extern volatile bool lapseRunning;
// 2 FPS in the resulting video
extern size_t videoFPS;
// Take a picture every second
extern size_t millisBetweenSnapshots;