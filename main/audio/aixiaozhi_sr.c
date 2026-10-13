#include "aixiaozhi_sr.h"

#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "audio/aixiaozhi_codec.h"
#include "esp_log.h"
#include "common/common_types.h"

#define TAG "aixiaozhi_sr"

static esp_afe_sr_iface_t *afe_handle = NULL; // 音频前端的句柄
esp_afe_sr_data_t *afe_data = NULL;           // 音频前端的数据

void aixiaozhi_vad_cb_test(void)
{
    ESP_LOGI(TAG, "人声状态改变了 当前状态为：%s", aixiaozhi_handle.current_vad_state == VAD_SPEECH ? "说话" : "静音");
}

void aixiaozhi_sr_init(void)
{
    // 初始化es8311
    aixiaozhi_codec_init();

    // 初始化模型
    srmodel_list_t *models = esp_srmodel_init("model");

    // 初始化配置
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);

    afe_config->aec_init = false; // 禁用回声消除
    afe_config->se_init = false;  // 禁用人声增强
    afe_config->ns_init = false;  // 禁用噪音消除

    afe_config->vad_min_noise_ms = 500;  // 设置vad最小静音时间为500ms
    afe_config->vad_min_speech_ms = 128; // 设置vad最小语音时间为128ms
    afe_config->vad_mode = VAD_MODE_2;   // 设置vad模式为2

    afe_config->wakenet_mode = DET_MODE_95;                      // 提升唤醒灵敏度
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM; // 将内存分配到外部的sram中

    // 获取afe(音频前端的句柄)
    afe_handle = esp_afe_handle_from_config(afe_config);
    // 创建实例
    afe_data = afe_handle->create_from_config(afe_config); // 建立一套方便后续使用的模板，当看到afe_data时，调用相应的配置

    // 启动喂数据和取数据的任务
    xTaskCreatePinnedToCoreWithCaps(aixiaozhi_sr_feed_task, "feed_task", 8 * 1024, NULL, 15, NULL, 0, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(aixiaozhi_sr_fetch_task, "fetch_task", 4 * 1024, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);
}

void aixiaozhi_sr_feed_task(void *arg)
{
    // 喂数据
    // 1.根据模型配置信息 得知这个模型一次需要多少采样点的数据来喂
    int feed_chunksize = afe_handle->get_feed_chunksize(afe_data);
    // 2.这个模型需要多少个通道
    int num = afe_handle->get_feed_channel_num(afe_data);
    // 3.分配一个喂数据的缓冲区
    int16_t *feed_buffer = (int16_t *)malloc(feed_chunksize * num * sizeof(int16_t)); // 单通道采样点X通道数X每个采样点的字节数
    while (1)
    {
        // 4.es8211里获取麦克风数据
        aixiaozhi_codec_record((uint8_t *)feed_buffer, feed_chunksize * num * sizeof(int16_t));
        // 5.把数据喂给模型
        afe_handle->feed(afe_data, feed_buffer);
    }
}

void aixiaozhi_sr_fetch_task(void *arg)
{
    while (1)
    {
        // 取数据
        // 1.从模型里面获取结果
        afe_fetch_result_t *result = afe_handle->fetch(afe_data);
        // 2.根据结果做相应的处理
        int16_t *processed_audio = result->data; // 处理后的音频数据
        // 3.有没有人声
        vad_state_t vad_state = result->vad_state;
        // 4.有没有唤醒词
        wakenet_state_t wakeup_state = result->wakeup_state;

        if (wakeup_state == WAKENET_DETECTED)
        {
            ESP_LOGI(TAG, "检测到唤醒词！");
            aixiaozhi_handle.wake_flag = true;
            aixiaozhi_handle.last_vad_state = VAD_SILENCE;
            aixiaozhi_handle.current_vad_state = VAD_SILENCE;
            // 执行回调
            if (aixiaozhi_handle.wake_callback != NULL)
            {
                aixiaozhi_handle.wake_callback();
            }
        }

        // 关注的是关键词唤醒后的人声状态的改变
        if (aixiaozhi_handle.wake_flag == true)
        {
            // 更新当前状态，唤醒后才更新
            aixiaozhi_handle.current_vad_state = vad_state;
            if (aixiaozhi_handle.current_vad_state != aixiaozhi_handle.last_vad_state)
            {
                if (aixiaozhi_handle.vad_changed_callback != NULL)
                {
                    aixiaozhi_handle.vad_changed_callback();
                }
            }
            // 更新上一个状态，当前状态作为上一个状态
            aixiaozhi_handle.last_vad_state = aixiaozhi_handle.current_vad_state;
        }

        // 既要唤醒也要有人声，满足这两个条件音频才有必要向后发送数据
        if (aixiaozhi_handle.wake_flag == true && vad_state == VAD_SPEECH)
        {
            if (result->vad_cache_size > 0)
            {
                int16_t *vad_cache = result->vad_cache;
                xRingbufferSend(aixiaozhi_handle.sr2encoder_ringbuffer_handle, vad_cache, result->vad_cache_size, portMAX_DELAY);
            }
            xRingbufferSend(aixiaozhi_handle.sr2encoder_ringbuffer_handle, processed_audio, result->data_size, portMAX_DELAY);
        }
    }
}
