#pragma once

#include "button_gpio.h"
#include "iot_button.h"
#include "button_adc.h"
#include "esp_log.h"

void aixiaozhi_button_front_register_cb(button_event_t btn_event, button_cb_t cb, void *usr_data);

void aixiaozhi_button_back_register_cb(button_event_t btn_event, button_cb_t cb, void *usr_data);

void aixiaozhi_button_init(void);
