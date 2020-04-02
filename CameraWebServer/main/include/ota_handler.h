#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void checkForOTA(const char *firmware_upgrade_url, int ota_recv_timeout, const char *ota_server_pem_start, bool skip_cert_common_name_check);

#ifdef __cplusplus
}
#endif
