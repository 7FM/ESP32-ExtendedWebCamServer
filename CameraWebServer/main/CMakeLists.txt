# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS "CameraWebServer.cpp" "http_server.cpp")
set(COMPONENT_ADD_INCLUDEDIRS "")
idf_build_get_property(project_dir PROJECT_DIR)
set(COMPONENT_EMBED_TXTFILES ${project_dir}/ota_server_ca.pem)

register_component()
