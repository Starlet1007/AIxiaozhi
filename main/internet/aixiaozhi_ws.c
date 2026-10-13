#include "aixiaozhi_ws.h"
#include "aixiaozhi_display.h"
#include "common/common_types.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "esp_log.h"

#define TAG "aixiaozhi_ws"

char *hello_json_str =
#include "pre_data/aixiaozhi_hello.txt"
    ;

char *detect_json_str =
#include "pre_data/aixiaozhi_detect.txt"
    ;

char *start_listen_json_str =
#include "pre_data/aixiaozhi_start_listen.txt"
    ;

char *stop_listen_json_str =
#include "pre_data/aixiaozhi_stop_listen.txt"
    ;

char *abort_json_str =
#include "pre_data/aixiaozhi_abort.txt"
    ;

char *iot_desc_json_str =
#include "pre_data/aixiaozhi_iot_desc.txt"
    ;

char *iot_state_json_str =
#include "pre_data/aixiaozhi_iot_state.txt"
    ;

esp_websocket_client_handle_t ws_client; // 目标客户端句柄

static void websocket_event_handler(void *handler_args, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id)
    {
    case WEBSOCKET_EVENT_BEGIN: // 尝试建立握手
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
        break;

    case WEBSOCKET_EVENT_CONNECTED: // 连接成功
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        // 只有在真正连接成功时，才给事件标志位置位
        xEventGroupSetBits(aixiaozhi_handle.event_group_handle, AIXIAOZHI_EVENT_WS_CONNECTED);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED: // 连接断开
        ESP_LOGW(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        // 连接断开时，必须清除连接标志位，防止其他任务盲目发包
        xEventGroupClearBits(aixiaozhi_handle.event_group_handle, AIXIAOZHI_EVENT_WS_CONNECTED);
        break;

    case WEBSOCKET_EVENT_DATA: // 收到数据
        // 只有在收到数据时，读取 op_code 和指针才是绝对安全的
        // 文本帧（服务器返回的字幕、表情、TTS 控制信令）
        if (data->op_code == 0x01)
        {
            ESP_LOGI(TAG, "Received text: %.*s", data->data_len, data->data_ptr);
            aixiaozhi_handle.ws_textframe_process(data->data_ptr, data->data_len);
        }
        // 二进制帧（服务器返回的 Opus 音频流）
        else if (data->op_code == 0x02)
        {
            ESP_LOGI(TAG, "Received binary frame, len: %d", data->data_len);
            aixiaozhi_handle.ws_binaryframe_process(data->data_ptr, data->data_len);
        }
        break;

    case WEBSOCKET_EVENT_ERROR: // 错误事件
        ESP_LOGE(TAG, "WEBSOCKET_EVENT_ERROR");
        aixiaozhi_handle.current_status = AIXIAOZHI_STATUS_IDLE;
        aixiaozhi_handle.wake_flag = false;
        break;

    case WEBSOCKET_EVENT_FINISH: // 完成事件
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
        aixiaozhi_handle.current_status = AIXIAOZHI_STATUS_IDLE;
        aixiaozhi_handle.wake_flag = false;
        break;

    default:
        break;
    }
}

void aixiaozhi_ws_init(void)
{
    ESP_LOGI(TAG, "初始化 WebSocket 客户端");
    ESP_LOGI(TAG, "WebSocket URL: %s", aixiaozhi_handle.websocket_url);

    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = aixiaozhi_handle.websocket_url;
    // 1.跟wss有关
    websocket_cfg.transport = WEBSOCKET_TRANSPORT_OVER_SSL;
    websocket_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    // 跳过证书主机名验证（解决证书验证问题）
    websocket_cfg.skip_cert_common_name_check = true;
    websocket_cfg.reconnect_timeout_ms = 1000;
    websocket_cfg.network_timeout_ms = 1000;

    // 2.完成配饰后直接初始化
    ws_client = esp_websocket_client_init(&websocket_cfg);

    // 根据websocket_token生成分配刚好的堆空间，然后在这个空间里面生成目标的字符串
    uint32_t auth_header_str_len = strlen("Bearer ") + strlen(aixiaozhi_handle.websocket_token);
    char *auth_header_str = heap_caps_malloc(auth_header_str_len + 1, MALLOC_CAP_SPIRAM);
    auth_header_str[auth_header_str_len] = '\0';
    sprintf(auth_header_str, "Bearer %s", aixiaozhi_handle.websocket_token);
    esp_websocket_client_append_header(ws_client, "Authorization", auth_header_str);
    esp_websocket_client_append_header(ws_client, "Protocol-Version", "1");
    esp_websocket_client_append_header(ws_client, "Device-ID", aixiaozhi_handle.mac_str);
    esp_websocket_client_append_header(ws_client, "Client-ID", aixiaozhi_handle.uuid);

    // 注册事件回调
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)ws_client);
}

bool aixiaozhi_ws_open_channel(void) // 确保websocket通道已连接
{
    if (esp_websocket_client_is_connected(ws_client))
    {
        return true;
    }
    else
    {
        esp_websocket_client_start(ws_client); // 建立后台线程，用于处理连接，升级和后续的双向对话

        // 等待一个事件标志位
        EventBits_t bits = xEventGroupWaitBits(aixiaozhi_handle.event_group_handle, AIXIAOZHI_EVENT_WS_CONNECTED, pdTRUE, pdFALSE, 5000 / portTICK_PERIOD_MS);
        if (!(bits & AIXIAOZHI_EVENT_WS_CONNECTED))
        {
            // 超时，没有等到
            aixiaozhi_display_text("woc 网不好");
            aixiaozhi_display_emoji("crying");
            return false;
        }
        return true;
    }
}

void aixiaozhi_ws_send_txt(char *data) // 向云端发送文本帧
{
    esp_websocket_client_send_text(ws_client, data, strlen(data), 2000);
}

void aixiaozhi_ws_send_binary(char *data, size_t data_len) // 向云端发送二进制帧
{
    esp_websocket_client_send_bin(ws_client, data, data_len, 2000);
}

void aixiaozhi_ws_send_hello(void) // 建立会话连接
{
    aixiaozhi_ws_send_txt(hello_json_str);
}

void aixiaozhi_ws_send_detect(void) // 检测用户唤醒
{
    aixiaozhi_ws_send_txt(detect_json_str);
}

void aixiaozhi_ws_send_start_listen(void) // 开始推送音频
{
    aixiaozhi_ws_send_txt(start_listen_json_str);
}

void aixiaozhi_ws_send_stop_listen(void) // 结束说话
{
    aixiaozhi_ws_send_txt(stop_listen_json_str);
}

void aixiaozhi_ws_send_abort(void) // 强行打断当前对话
{
    aixiaozhi_ws_send_txt(abort_json_str);
}

void aixiaozhi_ws_send_iot_desc(void) // 发送iot描述帧
{
    aixiaozhi_ws_send_txt(iot_desc_json_str);
}

void aixiaozhi_ws_send_iot_state(void) // 发送iot状态帧
{
    aixiaozhi_ws_send_txt(iot_state_json_str);
}
