#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void registerFSHandler(httpd_handle_t camera_httpd);

#ifdef __cplusplus
}
#endif
