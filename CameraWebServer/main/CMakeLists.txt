# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "CameraWebServer.cpp" "http_server.cpp")
set(COMPONENT_ADD_INCLUDEDIRS "")
idf_build_get_property(project_dir PROJECT_DIR)
set(COMPONENT_EMBED_TXTFILES ${project_dir}/ota_server_ca.pem)
set(COMPONENT_EMBED_FILES
        "${project_dir}/www/index_ov2640.html.gz"
        "${project_dir}/www/index_ov3660.html.gz"
        "${project_dir}/www/index_ov5640.html.gz"
        "${project_dir}/www/monitor.html.gz")

register_component()
