#pragma once
#include "stdbool.h"
#include "stddef.h"

void aixiaozhi_ws_init(void);
void aixiaozhi_ws_send_hello(void);
void aixiaozhi_ws_send_detect(void);
void aixiaozhi_ws_send_start_listen(void);
void aixiaozhi_ws_send_stop_listen(void);
void aixiaozhi_ws_send_abort(void);
void aixiaozhi_ws_send_iot_desc(void);
void aixiaozhi_ws_send_iot_state(void);
void aixiaozhi_ws_send_binary(char *data, size_t data_len);

bool aixiaozhi_ws_open_channel(void);
