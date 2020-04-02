set(COMPONENT_REQUIRES
    esp32-camera
    nvs_flash
    esp_http_server
    mdns
    fatfs
    esp_https_ota
    app_update
    )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "CameraWebServer.cpp" "http_server.cpp" "config_reader.cpp" "wifi_helper.c" "mdns_helper.c" "camera_helper.c" "fs_browser.c" "lapse_handler.cpp" "ota_handler.c" "WString.cpp" "web_utils.c")
set(COMPONENT_ADD_INCLUDEDIRS "include")
idf_build_get_property(project_dir PROJECT_DIR)
set(COMPONENT_EMBED_TXTFILES ${project_dir}/ota_server_ca.pem)
set(COMPONENT_EMBED_FILES
        "${project_dir}/www/index_ov2640.html.gz"
        "${project_dir}/www/index_ov3660.html.gz"
        "${project_dir}/www/index_ov5640.html.gz"
        "${project_dir}/www/monitor.html.gz")

register_component()
