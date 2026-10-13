#pragma once
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "nvs.h"

void aixiaozhi_active_request(void);

void aixiaozhi_active_set_request_header(void);

void aixiaozhi_active_uuid_v4_gen(char *uuid_str);

void aixiaozhi_active_set_request_body(void);

void aixiaozhi_active_parse_response(char *res, int len);
