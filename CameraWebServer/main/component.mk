#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)
COMPONENT_EMBED_TXTFILES := ${PROJECT_PATH}/ota_server_ca.pem

COMPONENT_EMBED_FILES := ${PROJECT_PATH}/www/index_ov2640.html.gz
COMPONENT_EMBED_FILES += ${PROJECT_PATH}/www/index_ov3660.html.gz
COMPONENT_EMBED_FILES += ${PROJECT_PATH}/www/index_ov5640.html.gz
COMPONENT_EMBED_FILES += ${PROJECT_PATH}/www/monitor.html.gz