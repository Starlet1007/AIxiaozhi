#include "aixiaozhi_opus_encoder.h"
#include "esp_opus_enc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "common/common_types.h"
#include "esp_log.h"
#include <string.h>

#define TAG "aixiaozhi_opus_encoder"

esp_audio_enc_handle_t encodeer = NULL;

void aixiaozhi_opus_encoder_task(void *arg);

int pcm_input_size = 0;
int opus_output_size = 0;
uint8_t *pcm_data_buffer;
uint8_t *opus_data_buffer;

void aixiaozhi_opus_encoder_init(void) // 初始化opus编码器
{
    esp_opus_enc_config_t opus_cfg =
        {
            .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
            .channel = ESP_AUDIO_MONO,
            .bits_per_sample = ESP_AUDIO_BIT16,
            .bitrate = 32000,
            .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
            .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
            .complexity = 5,
            .enable_fec = false,
            .enable_dtx = false,
            .enable_vbr = false,
        };
    esp_opus_enc_open(&opus_cfg, sizeof(esp_opus_enc_config_t), &encodeer);
    esp_opus_enc_get_frame_size(encodeer, &pcm_input_size, &opus_output_size);
    ESP_LOGI(TAG, "pcm_input_size: %d, opus_output_size: %d", pcm_input_size, opus_output_size);

    // 创建encoder的两个小缓冲区
    pcm_data_buffer = heap_caps_malloc(pcm_input_size, MALLOC_CAP_SPIRAM);
    opus_data_buffer = heap_caps_malloc(opus_output_size, MALLOC_CAP_SPIRAM);

    xTaskCreatePinnedToCoreWithCaps(aixiaozhi_opus_encoder_task, "aixiaozhi_opus_encoder_task", 32 * 1024, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);
}

// 不停的从环形缓冲区拉数据来进行编码
void aixiaozhi_opus_encoder_task(void *arg)
{
    esp_audio_enc_in_frame_t in_frame = // 标准参数的清单
        {
            .buffer = pcm_data_buffer, // 指向缓冲区的首地址
            .len = pcm_input_size,     // 缓冲区的大小
        };

    esp_audio_enc_out_frame_t out_frame =
        {
            .buffer = opus_data_buffer,
            .len = opus_output_size,
        };
    while (1)
    {
        size_t remain_frame_size = pcm_input_size;
        uint8_t *current_buffer_start_position = in_frame.buffer; // 当前缓冲区的起始位置
        while (remain_frame_size > 0)
        {
            // 从环形缓冲区中1920字节的读取数据

            size_t this_time_read_size = 0; // 当前读取的字节数
            // 只要环形缓冲区有数据，哪怕不满1920，本函数也会结束，指向环形缓冲区中要复制的数据
            void *data_pointer = xRingbufferReceiveUpTo(aixiaozhi_handle.sr2encoder_ringbuffer_handle, &this_time_read_size, portMAX_DELAY, remain_frame_size);
            // 把环形缓冲区内的数据拷贝到in_frame.buffer中
            memcpy(current_buffer_start_position, data_pointer, this_time_read_size);
            // 取消对环形缓冲区的占用（释放）
            vRingbufferReturnItem(aixiaozhi_handle.sr2encoder_ringbuffer_handle, data_pointer);
            // 向后移动指针，避免每次都把数据拷贝到in_frame的开始位置
            current_buffer_start_position += this_time_read_size;
            // 计算60ms帧还剩多少字节
            remain_frame_size -= this_time_read_size;
        }
        // 开始压缩成opus格式
        esp_opus_enc_process(encodeer, &in_frame, &out_frame);
        // 发送给下一个环形缓冲区
        xRingbufferSend(aixiaozhi_handle.encoder_out_ringbuffer_handle, out_frame.buffer, out_frame.encoded_bytes, portMAX_DELAY);
    }
}
