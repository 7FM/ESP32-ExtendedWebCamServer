#include "esp_http_server.h"
#include <dirent.h>
#include <sys/stat.h>

// Local files
#include "makros.h"
#include "web_utils.h"
#include "fs_browser.h"

static inline bool isDir(struct dirent *entry) {
    return entry->d_type == DT_DIR;
}
/*
static inline bool isDir(struct stat *file_stat) {
    return S_ISDIR(file_stat->st_mode);
}*/

#define DIR_TYPE 2
#define FILE_TYPE 1
#define NONE_TYPE 0

static inline int getType(char *absolutePath) {
    struct stat file_stat;

    if (stat(absolutePath, &file_stat)) {
        return NONE_TYPE;
    }

    return S_ISDIR(file_stat.st_mode) ? DIR_TYPE : FILE_TYPE;
}

static inline bool listDirectory(const char *absolutePath, bool (*callback)(const char * /*entryName*/, bool /*isDirectory*/, void * /*arg*/), void *arg) {
    DIR *dp = opendir(absolutePath);

    if (dp == NULL) {
        return false;
    }

    struct dirent *entry;
    bool res = true;
    while ((entry = readdir(dp)) && (res = callback(entry->d_name, isDir(entry), arg))) {
    }

    closedir(dp);

    return res;
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

    //TODO adjustable?
    char filepath[256];

    char *buf = NULL;
    if (parse_get(req, &buf) != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_query_key_value(buf, "path", filepath, sizeof(filepath)) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    free(buf);

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

void registerFSHandler(httpd_handle_t camera_httpd) {

    httpd_uri_t fs_uri = {.uri = "/fs",
                          .method = HTTP_GET,
                          .handler = filesystem_handler,
                          .user_ctx = NULL};

    httpd_register_uri_handler(camera_httpd, &fs_uri);
}