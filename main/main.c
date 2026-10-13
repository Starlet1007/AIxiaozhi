#include <stdio.h>
#include <stdint.h>
#include "button/aixiaozhi_button.h"
#include "internet/aixiaozhi_wifi.h"
#include "display/aixiaozhi_display.h"
#include "esp_log.h"
#include "internet/aixiaozhi_active.h"
#include "common/common_types.h"
#include "audio/aixiaozhi_codec.h"
#include "audio/aixiaozhi_sr.h"
#include "audio/aixiaozhi_opus_encoder.h"
#include "audio/aixiaozhi_opus_decoder.h"
#include "internet/aixiaozhi_ws.h"

aixiaozhi_handle_t aixiaozhi_handle;

void wake_callback(void) // 当检测用户唤醒时，调用opus_channel确保网络连接正常
{
    if (aixiaozhi_handle.current_status == AIXIAOZHI_STATUS_IDLE)
    {
        bool connected = aixiaozhi_ws_open_channel();
        if (connected)
        {
            aixiaozhi_ws_send_hello();
            aixiaozhi_ws_send_iot_desc();
            aixiaozhi_ws_send_iot_state();

            // 等待服务器回hello
            EventBits_t bits = xEventGroupWaitBits(aixiaozhi_handle.event_group_handle, AIXIAOZHI_EVENT_RECEIVE_HELLO, true, true, 5000);
            if (bits & AIXIAOZHI_EVENT_RECEIVE_HELLO)
            {
                aixiaozhi_ws_send_detect();
            }
            else
            {
                aixiaozhi_display_text("服务器好没礼貌");
                return;
            }
        }
    }
    else if (aixiaozhi_handle.current_status == AIXIAOZHI_STATUS_SPEAKING)
    {
        aixiaozhi_ws_send_abort();
    }
}

void ws_textframe_process(char *data, int data_len) // 当云端给硬件发送数据时，解析json数据
{
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL)
    {
        ESP_LOGE("main", "woc 服务器不讲json");
        return;
    }
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (type == NULL)
    {
        ESP_LOGE("main", "woc 缺少type");
        return;
    }

    // 处理服务器发来的hello json
    if (strcmp(type->valuestring, "hello") == 0)
    {
        xEventGroupSetBits(aixiaozhi_handle.event_group_handle, AIXIAOZHI_EVENT_RECEIVE_HELLO);
    }

    // 处理服务器发来的句子字幕
    if (strcmp(type->valuestring, "tts") == 0)
    {
        cJSON *tts_state = cJSON_GetObjectItem(root, "state");
        if (strcmp(tts_state->valuestring, "sentence_start") == 0)
        {
            // 说明这是一个句子的开始，然后text里面一定有字幕可以显示
            cJSON *text = cJSON_GetObjectItem(root, "text");
            // 在屏幕上显示字幕
            aixiaozhi_display_text(text->valuestring);
        }
        // 判断是不是tts start
        if (strcmp(tts_state->valuestring, "start") == 0)
        {
            aixiaozhi_handle.current_status = AIXIAOZHI_STATUS_SPEAKING;
        }
        // 判断是不是tts stop
        if (strcmp(tts_state->valuestring, "stop") == 0)
        {
            aixiaozhi_handle.current_status = AIXIAOZHI_STATUS_IDLE;
        }
    }

    // 处理服务器发来的表情
    if (strcmp(type->valuestring, "llm") == 0)
    {
        cJSON *emotion = cJSON_GetObjectItem(root, "emotion");
        aixiaozhi_display_emoji(emotion->valuestring);
    }
    // 必须删除json，否则随着多次对话会产生大量的野内存和碎片

    // 处理服务器发来的iot指令
    if (strcmp(type->valuestring, "iot") == 0)
    {
        cJSON *cmdsItem = cJSON_GetObjectItem(root, "commands");
        cJSON *cmdItem = cJSON_GetArrayItem(cmdsItem, 0);
        cJSON *parametersItem = cJSON_GetObjectItem(cmdItem, "parameters");

        char *method = cJSON_GetObjectItem(cmdItem, "method")->valuestring;

        if (strcmp(method, "SetVolume") == 0)
        {
            int volumne = cJSON_GetObjectItem(parametersItem, "volume")->valueint;

            aixiaozhi_codec_set_volume(volumne);
        }
        else if (strcmp(method, "SetMute") == 0)
        {
            cJSON *muteItem = cJSON_GetObjectItem(parametersItem, "mute");
            bool is_mute = false;
            if (cJSON_IsBool(muteItem))
            {
                is_mute = cJSON_IsTrue(muteItem);
            }
            aixiaozhi_codec_set_mute(is_mute);
        }
    }

    cJSON_Delete(root);
}

void ws_binaryframe_process(char *data, size_t data_len) // 将云端给硬件发送的二进制数据发送给解码器
{
    xRingbufferSend(aixiaozhi_handle.decoder_in_ringbuffer_handle, data, data_len, portMAX_DELAY);
}

// 发送音频任务
void ws_send_audio_task(void *arg)
{
    while (1)
    {
        size_t data_size = 0;
        void *data = xRingbufferReceive(aixiaozhi_handle.encoder_out_ringbuffer_handle, &data_size, portMAX_DELAY);

        if (aixiaozhi_handle.current_status == AIXIAOZHI_STATUS_LISTENING && data_size > 0)
        {
            aixiaozhi_ws_send_binary(data, data_size);
        }

        vRingbufferReturnItem(aixiaozhi_handle.encoder_out_ringbuffer_handle, data);
    }
}

// 自制回声消除，防止小智通过扬声器自己说的，导致服务器收到自己的语音
void vad_changed_cb(void)
{
    if (aixiaozhi_handle.current_vad_state == VAD_SPEECH)
    {
        // 小智把话说完才能进入listening状态
        if (aixiaozhi_handle.current_status == AIXIAOZHI_STATUS_IDLE)
        {
            aixiaozhi_handle.current_status = AIXIAOZHI_STATUS_LISTENING;
            aixiaozhi_ws_send_start_listen();
        }
    }
    else if (aixiaozhi_handle.current_vad_state == VAD_SILENCE)
    {
        // 人声可能是小智通过扬声器自己说的
        if (aixiaozhi_handle.current_status != AIXIAOZHI_STATUS_SPEAKING)
        {
            aixiaozhi_handle.current_status = AIXIAOZHI_STATUS_IDLE;
            aixiaozhi_ws_send_stop_listen();
        }
    }
}

void button_cb(void *button_hanlde, void *usr_data)
{
    uint8_t event = (uint8_t)usr_data;
    if (event == 1)
    {
        // 前面的按键单击
        ESP_LOGI("button", "front button single click");
        if (aixiaozhi_handle.active_flag == false)
        {
            // 在没有被激活的状态下，这个按键的功能是重启整个设备
            esp_restart();
        }
    }
    if (event == 2)
    {
        // 后面的按键单击
        ESP_LOGI("button", "back button single click");
    }
    if (event == 3)
    {
        // 后面的按键长按
        ESP_LOGI("button", "back button long press start");
        aixiaozhi_wifi_reset();
    }
}

void aixiaozhi_create_all_ringbuffer(void)
{
    aixiaozhi_handle.sr2encoder_ringbuffer_handle = xRingbufferCreateWithCaps(8 * 1024, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    aixiaozhi_handle.encoder_out_ringbuffer_handle = xRingbufferCreateWithCaps(16 * 1024, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM);
    aixiaozhi_handle.decoder_in_ringbuffer_handle = xRingbufferCreateWithCaps(16 * 1024, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_SPIRAM);
}

void encoder_decoder_test_task(void *arg)
{
    while (1)
    {
        size_t data_size = 0;
        void *data = xRingbufferReceive(aixiaozhi_handle.encoder_out_ringbuffer_handle, &data_size, portMAX_DELAY);
        xRingbufferSend(aixiaozhi_handle.decoder_in_ringbuffer_handle, data, data_size, portMAX_DELAY);
        vRingbufferReturnItem(aixiaozhi_handle.encoder_out_ringbuffer_handle, data);
    }
}

void app_main(void)
{
    aixiaozhi_create_all_ringbuffer();

    // 创建一个用以管理小智业务的事件标志组
    aixiaozhi_handle.event_group_handle = xEventGroupCreate();
    aixiaozhi_handle.wake_callback = wake_callback;
    aixiaozhi_handle.ws_textframe_process = ws_textframe_process;
    aixiaozhi_handle.ws_binaryframe_process = ws_binaryframe_process;
    aixiaozhi_handle.vad_changed_callback = vad_changed_cb;

    // 1.各种初始化
    // 1.1 初始化lvgl
    aixiaozhi_display_init();
    // 1.2 初始化wifi
    aixiaozhi_wifi_init();
    // 1.3 初始化按键
    aixiaozhi_button_init();
    aixiaozhi_button_front_register_cb(BUTTON_SINGLE_CLICK, button_cb, (void *)1);
    aixiaozhi_button_back_register_cb(BUTTON_SINGLE_CLICK, button_cb, (void *)2);
    aixiaozhi_button_back_register_cb(BUTTON_LONG_PRESS_START, button_cb, (void *)3);
    // 1.4 配网成功删除二维码
    aixiaozhi_display_delete_qrcode();

    // 2.请求激活
    aixiaozhi_active_request();
    // 2.1 aixiaozhi的句柄里现在有激活信息
    if (aixiaozhi_handle.active_flag == false)
    {
        char *temp_buffer = (char *)calloc(60, 1);
        sprintf(temp_buffer, "请到官网控制台激活设备\n 激活码：%s", aixiaozhi_handle.active_code);
        aixiaozhi_display_text(temp_buffer);
        free(temp_buffer);
        aixiaozhi_display_tip("激活后请单击前按键");
    }

    // 刚开机时为空闲状态
    aixiaozhi_handle.current_status = AIXIAOZHI_STATUS_IDLE;
    aixiaozhi_sr_init();

    aixiaozhi_opus_encoder_init();
    aixiaozhi_opus_decoder_init();

    // xTaskCreate(encoder_decoder_test_task, "encoder_decoder_test_task", 4096, NULL, 5, NULL);
    xTaskCreatePinnedToCoreWithCaps(ws_send_audio_task, "ws_send_audio_task", 4096, NULL, 16, NULL, 1, MALLOC_CAP_SPIRAM);
    aixiaozhi_ws_init();
}
