#include "aixiaozhi_button.h"

#define TAG "aixiaozhi_button"

button_handle_t sw2_adc_btn_handle;
button_handle_t sw3_adc_btn_handle;

void aixiaozhi_button_init(void)
{
    // 创建sw2 ADC前按键 GND
    button_config_t sw2_btn_cfg = {0};
    button_adc_config_t sw2_btn_adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .adc_channel = 7,
        .button_index = 0,
        .min = 0,
        .max = 500,
    };

    sw2_adc_btn_handle = NULL;
    iot_button_new_adc_device(&sw2_btn_cfg, &sw2_btn_adc_cfg, &sw2_adc_btn_handle);

    if (sw2_adc_btn_handle == NULL)
    {
        ESP_LOGE(TAG, "sw2 Button create failed");
    }

    // 创建sw3 ADC后按键 1.65v
    button_config_t sw3_btn_cfg = {0};
    button_adc_config_t sw3_btn_adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .adc_channel = 7,
        .button_index = 1,
        .min = 1000,
        .max = 2400,
    };

    sw3_adc_btn_handle = NULL;
    iot_button_new_adc_device(&sw3_btn_cfg, &sw3_btn_adc_cfg, &sw3_adc_btn_handle);

    if (sw3_adc_btn_handle == NULL)
    {
        ESP_LOGE(TAG, "sw3 Button create failed");
    }
}

// 给前面的按键(sw2)注册回调函数
void aixiaozhi_button_front_register_cb(button_event_t btn_event, button_cb_t cb, void *usr_data)
{
    iot_button_register_cb(sw2_adc_btn_handle, btn_event, NULL, cb, usr_data);
}

// 给后面的按键(sw3)注册回调函数
void aixiaozhi_button_back_register_cb(button_event_t btn_event, button_cb_t cb, void *usr_data)
{
    iot_button_register_cb(sw3_adc_btn_handle, btn_event, NULL, cb, usr_data);
}
