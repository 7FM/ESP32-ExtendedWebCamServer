#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

bool urldecode(char *str, size_t length);
esp_err_t parse_get(httpd_req_t *req, char **obuf);

int parse_get_var(char *buf, const char *key, int def);

#ifdef __cplusplus
}
#endif