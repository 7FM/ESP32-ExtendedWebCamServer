
#if defined(CAMERA_MODEL_WROVER_KIT)
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    21
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      19
#define CAM_PIN_D2      18
#define CAM_PIN_D1      5
#define CAM_PIN_D0      4
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

#elif defined(CAMERA_MODEL_ESP_EYE)
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    4
#define CAM_PIN_SIOD    18
#define CAM_PIN_SIOC    23

#define CAM_PIN_D7      36
#define CAM_PIN_D6      37
#define CAM_PIN_D5      38
#define CAM_PIN_D4      39
#define CAM_PIN_D3      35
#define CAM_PIN_D2      14
#define CAM_PIN_D1      13
#define CAM_PIN_D0      34
#define CAM_PIN_VSYNC   5
#define CAM_PIN_HREF    27
#define CAM_PIN_PCLK    25

#elif defined(CAMERA_MODEL_M5STACK_PSRAM)
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   15
#define CAM_PIN_XCLK    27
#define CAM_PIN_SIOD    25
#define CAM_PIN_SIOC    23

#define CAM_PIN_D7      19
#define CAM_PIN_D6      36
#define CAM_PIN_D5      18
#define CAM_PIN_D4      39
#define CAM_PIN_D3      5
#define CAM_PIN_D2      34
#define CAM_PIN_D1      35
#define CAM_PIN_D0      32
#define CAM_PIN_VSYNC    22
#define CAM_PIN_HREF     26
#define CAM_PIN_PCLK     21

#elif defined(CAMERA_MODEL_M5STACK_WIDE)
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   15
#define CAM_PIN_XCLK    27
#define CAM_PIN_SIOD    22
#define CAM_PIN_SIOC    23

#define CAM_PIN_D7      19
#define CAM_PIN_D6      36
#define CAM_PIN_D5      18
#define CAM_PIN_D4      39
#define CAM_PIN_D3      5
#define CAM_PIN_D2      34
#define CAM_PIN_D1      35
#define CAM_PIN_D0      32
#define CAM_PIN_VSYNC    25
#define CAM_PIN_HREF     26
#define CAM_PIN_PCLK     21

#elif defined(CAMERA_MODEL_AI_THINKER)
#define CAM_PIN_PWDN     32
#define CAM_PIN_RESET    -1
#define CAM_PIN_XCLK      0
#define CAM_PIN_SIOD     26
#define CAM_PIN_SIOC     27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC    25
#define CAM_PIN_HREF     23
#define CAM_PIN_PCLK     22

#else
#error "Camera model not selected"
#endif
