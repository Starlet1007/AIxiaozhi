#include "aixiaozhi_codec.h"

i2c_master_bus_handle_t i2c0_bus_handle;

i2s_chan_handle_t i2s_tx_chan_handle;
i2s_chan_handle_t i2s_rx_chan_handle;

esp_codec_dev_handle_t codec_dev_handle;

void aixiaozhi_codec_i2c_init(void)
{
    i2c_master_bus_config_t i2c_bus_config = {0};
    i2c_bus_config.i2c_port = I2C_NUM_0;
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;

    i2c_bus_config.scl_io_num = GPIO_NUM_1;
    i2c_bus_config.sda_io_num = GPIO_NUM_0;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    i2c_new_master_bus(&i2c_bus_config, &i2c0_bus_handle);
}

void aixiaozhi_codec_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_3,
            .bclk = GPIO_NUM_2,
            .ws = GPIO_NUM_5,
            .dout = GPIO_NUM_6,
            .din = GPIO_NUM_4},
    };

    // 创建通道
    i2s_new_channel(&chan_cfg, &i2s_tx_chan_handle, &i2s_rx_chan_handle);
    // 初始化通道
    i2s_channel_init_std_mode(i2s_tx_chan_handle, &std_cfg);
    i2s_channel_init_std_mode(i2s_rx_chan_handle, &std_cfg);
    // 使能通道
    i2s_channel_enable(i2s_tx_chan_handle);
    i2s_channel_enable(i2s_rx_chan_handle);
}

void aixiaozhi_codec_init(void)
{
    aixiaozhi_codec_i2c_init();
    aixiaozhi_codec_i2s_init();

    audio_codec_i2s_cfg_t i2s_cfg =
        {
            .rx_handle = i2s_rx_chan_handle,
            .tx_handle = i2s_tx_chan_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg); // 创建i2s数据接口对象，负责音频的收发

    audio_codec_i2c_cfg_t i2c_cfg =
        {
            .addr = ES8311_CODEC_DEFAULT_ADDR,
            .bus_handle = i2c0_bus_handle,
    };
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg); // 创建i2c控制接口对象

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio(); // 创建gpio接口对象

    // 基于控制接口和ES83111特有1的配置实现audio_codec_if_t接口
    es8311_codec_cfg_t es8311_cfg =
        {
            .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
            .ctrl_if = out_ctrl_if,
            .gpio_if = gpio_if,
            .pa_pin = GPIO_NUM_7,
            .use_mclk = true,
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg); // 创建es8311编码器接口对象

    esp_codec_dev_cfg_t dev_cfg =
        {
            .codec_if = out_codec_if,              // es8311_codec_new 获取到的接口实现
            .data_if = data_if,                    // audio_codec_new_i2s_data 获取到的数据接口实现
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, // 设备同时支持录制和播放
    };
    codec_dev_handle = esp_codec_dev_new(&dev_cfg);

    // 以下代码展示如何播放音频
    esp_codec_dev_set_out_vol(codec_dev_handle, 40.0);
    esp_codec_dev_set_in_gain(codec_dev_handle, 15.0);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    esp_codec_dev_open(codec_dev_handle, &fs);
}

void aixiaozhi_codec_play(uint8_t *data, uint32_t data_size) // 播放pcm音频数据
{
    esp_codec_dev_write(codec_dev_handle, data, data_size);
}

void aixiaozhi_codec_record(uint8_t *data, uint32_t data_size) // 读取pcm音频数据
{
    esp_codec_dev_read(codec_dev_handle, data, data_size);
}

int last_volume = 0;
void aixiaozhi_codec_set_volume(int volume) // 设置音量
{
    last_volume = volume;
    esp_codec_dev_set_out_vol(codec_dev_handle, volume);
}

void aixiaozhi_codec_set_mute(bool mute) // 静音与一键恢复
{
    esp_codec_dev_set_out_vol(codec_dev_handle, mute == true ? 0 : last_volume);
}
