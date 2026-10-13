#pragma once

#include "esp_lvgl_port.h"
#include <stdint.h>

void aixiaozhi_display_init(void);

void aixiaozhi_show_qrcode(const char *data, uint32_t data_len);

void aixiaozhi_display_delete_qrcode(void);

void aixiaozhi_display_text(char *text);

void aixiaozhi_display_emoji(char *emoji_name);

void aixiaozhi_display_tip(char *tip);
