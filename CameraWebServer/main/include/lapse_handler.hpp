#pragma once

#include "esp_camera.h"

void lapseHandlerSetup();
int handleLapse(sensor_t *s, int lapse);