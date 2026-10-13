#pragma once
#include <stdbool.h>

#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"

typedef enum
{
    AIXIAOZHI_STATUS_IDLE,      // 空闲状态
    AIXIAOZHI_STATUS_LISTENING, // 小智正在听我们说
    AIXIAOZHI_STATUS_SPEAKING,  // 小智正在说话
} aixiaozhi_status_enum;

typedef struct
{
    char *websocket_url;
    char *websocket_token;
    bool active_flag;  // 激活状态
    char *active_code; // 激活码
    char *mac_str;     // 设备mac地址
    char *uuid;        // 这个也叫client_id

    // 项目中所有环形缓冲区句柄
    RingbufHandle_t sr2encoder_ringbuffer_handle;
    RingbufHandle_t encoder_out_ringbuffer_handle;
    RingbufHandle_t decoder_in_ringbuffer_handle;

    bool wake_flag; // 检测唤醒词状态
    vad_state_t last_vad_state;
    vad_state_t current_vad_state;

    void (*wake_callback)(void);
    void (*vad_changed_callback)(void); // VAD状态改变回调
    void (*ws_textframe_process)(char *data, int data_len);
    void (*ws_binaryframe_process)(char *data, size_t data_len);

    // ws相关的业务逻辑变量
    EventGroupHandle_t event_group_handle; // 事件标志组句柄

    aixiaozhi_status_enum current_status;
} aixiaozhi_handle_t;

#define AIXIAOZHI_EVENT_WS_CONNECTED BIT0
#define AIXIAOZHI_EVENT_RECEIVE_HELLO BIT1

typedef struct
{
    char *name; // 显示的文本
    char *emoji;
} aixiaozhi_emoji_t;

extern aixiaozhi_handle_t aixiaozhi_handle;
