#include "aixiaozhi_opus_decoder.h"
#include "esp_opus_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "common/common_types.h"
#include "aixiaozhi_codec.h"

esp_audio_dec_handle_t decoder_handle;

void xiaozhi_opus_decoder_task(void *arg);

void aixiaozhi_opus_decoder_init()
{
    esp_opus_dec_cfg_t cfg =
        {
            .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
            .channel = ESP_AUDIO_MONO,
            .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
            .self_delimited = false,
        };

    esp_opus_dec_open(&cfg, sizeof(cfg), &decoder_handle);

    xTaskCreatePinnedToCoreWithCaps(xiaozhi_opus_decoder_task, "xiaozhi_opus_decoder_task", 32 * 1024, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);
}

void xiaozhi_opus_decoder_task(void *arg)
{
    esp_audio_dec_out_frame_t out_pcm_frame = {0};
    out_pcm_frame.buffer = heap_caps_malloc(8 * 1024, MALLOC_CAP_SPIRAM);
    out_pcm_frame.len = 8 * 1024;

    esp_audio_dec_in_raw_t input_opus_frame = {0};

    esp_audio_dec_info_t dec_info = {0}; // 解码器信息（用不到，当作必须的参数处理）

    while (1)
    {
        size_t to_decode_data_len = 0;
        // 从环形缓冲区中拿到要解析的数据的长度和起始位置的指针
        void *to_decode_data_pointer = xRingbufferReceive(aixiaozhi_handle.decoder_in_ringbuffer_handle, &to_decode_data_len, portMAX_DELAY);
        input_opus_frame.buffer = to_decode_data_pointer;
        input_opus_frame.len = to_decode_data_len;
        while (input_opus_frame.len > 0)
        {
            esp_opus_dec_decode(decoder_handle, &input_opus_frame, &out_pcm_frame, &dec_info);

            // 播放解码后的数据
            aixiaozhi_codec_play(out_pcm_frame.buffer, out_pcm_frame.decoded_size);

            input_opus_frame.buffer += input_opus_frame.consumed; // 指针后移到下一次解析的位置
            input_opus_frame.len -= input_opus_frame.consumed;    // 剩余数据长度
        }
        // 解析完成后，将数据返回到环形缓冲区
        vRingbufferReturnItem(aixiaozhi_handle.decoder_in_ringbuffer_handle, to_decode_data_pointer);
    }
}
