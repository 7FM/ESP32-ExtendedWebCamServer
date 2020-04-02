#include "esp_http_server.h"
#include "web_utils.h"

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

bool urldecode(char *str, size_t length) {
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

esp_err_t parse_get(httpd_req_t *req, char **obuf) {
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = (char *)malloc(buf_len);
        if (!buf) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            *obuf = buf;
            return ESP_OK;
        }
        free(buf);
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}

int parse_get_var(char *buf, const char *key, int def) {
    char _int[16];
    if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK) {
        return def;
    }
    return atoi(_int);
}

#ifdef __cplusplus
}
#endif