#include "display/aixiaozhi_display.h"
#include "display/aixiaozhi_lcd.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "font_emoji.h"
#include "common/common_types.h"

#define TAG "aixiaozhi_display"

#define LCD_H_RES 320
#define LCD_V_RES 240

#define LCD_DRAW_BUFF_HEIGHT 10

static lv_display_t *lvgl_disp = NULL;

lv_obj_t *qr;
lv_obj_t *text_label;
lv_obj_t *emoji_label;
lv_obj_t *tip_label;

extern const lv_font_t font_puhui_20_4;

const aixiaozhi_emoji_t emoji_list[21] =
    {
        {.name = "neutral", .emoji = "😶"},
        {.name = "happy", .emoji = "🙂"},
        {.name = "laughing", .emoji = "😆"},
        {.name = "funny", .emoji = "😂"},
        {.name = "sad", .emoji = "😔"},
        {.name = "angry", .emoji = "😠"},
        {.name = "crying", .emoji = "😭"},
        {.name = "loving", .emoji = "😍"},
        {.name = "embarrassed", .emoji = "😳"},
        {.name = "surprised", .emoji = "😲"},
        {.name = "shocked", .emoji = "😱"},
        {.name = "thinking", .emoji = "🤔"},
        {.name = "winking", .emoji = "😉"},
        {.name = "cool", .emoji = "😎"},
        {.name = "relaxed", .emoji = "😌"},
        {.name = "delicious", .emoji = "🤤"},
        {.name = "kissy", .emoji = "😘"},
        {.name = "confident", .emoji = "😏"},
        {.name = "sleepy", .emoji = "😴"},
        {.name = "silly", .emoji = "😜"},
        {.name = "confused", .emoji = "🙄"}};

void aixiaozhi_display_init(void)
{
    aixiaozhi_lcd_init();

    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,       /* LVGL task priority */
        .task_stack = 6144,       /* LVGL task stack size */
        .task_affinity = -1,      /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500, /* Maximum sleep in LVGL task */
        .timer_period_ms = 5      /* LVGL timer tick period in ms */
    };
    lvgl_port_init(&lvgl_cfg);

    uint32_t buff_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT;

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = buff_size,
        .double_buffer = true,
        .hres = LCD_V_RES,
        .vres = LCD_H_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,

        }};
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // 1. 创建一个专门用来显示字幕的区域
    lvgl_port_lock(0);
    lv_obj_t *screen = lv_screen_active(); // screen代表这个屏幕，这个屏幕的参数注册给这个结构体变量，直接调用screen就可以获取这个屏幕的具体参数
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xD4D6DC), LV_PART_MAIN);

    // 1.1 创建一个提示标签
    tip_label = lv_label_create(screen);
    // 1.2 设置文本内容
    lv_label_set_text(tip_label, "欢迎光临");
    // 1.3 设置标签的宽度为240 刚好跟屏幕一样宽
    lv_obj_set_style_width(tip_label, 240, LV_PART_MAIN);
    // 1.4 让整个标签文本框对到屏幕的正中间
    lv_obj_align(tip_label, LV_ALIGN_TOP_MID, 0, 0);
    // 1.5 设置标签的文本颜色为黑色
    lv_obj_set_style_text_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    // 1.6 标签中的文字居中对齐
    lv_obj_set_style_text_align(tip_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // 1.7 为这个标签挂载中文字库
    lv_obj_set_style_text_font(tip_label, &font_puhui_20_4, LV_PART_MAIN);
    // 1.8 设置标签的背景色为纯白色
    lv_obj_set_style_bg_color(tip_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    // 1.9 设置为不透明
    lv_obj_set_style_bg_opa(tip_label, LV_OPA_COVER, LV_PART_MAIN);

    // 2.0 创建一个表情标签
    emoji_label = lv_label_create(screen);
    // 2.1 设置一个表情
    lv_label_set_text(emoji_label, "🙂");
    // 2.2 这个表情以屏幕的正中心为基准，向上偏移70像素
    lv_obj_align(emoji_label, LV_ALIGN_CENTER, 0, -70);
    // 2.3 设置标签中文字（表情）的对齐方式 让它放在标签的正中间
    lv_obj_set_style_text_align(emoji_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // 2.4 挂在表情包字库
    lv_obj_set_style_text_font(emoji_label, font_emoji_64_init(), LV_PART_MAIN);

    // 3.0 创建屏幕中间显示字幕的文本框
    text_label = lv_label_create(screen);
    // 3.1 设置文本框中的文字内容
    lv_label_set_text(text_label, "您的专属ai助手");
    // 3.2 设置文本框240像素宽
    lv_obj_set_style_width(text_label, 240, LV_PART_MAIN);
    // 3.3 让这个文本框显示在表情包的正下方 并且保持10像素的间距(这个地方必须先设宽度 再对齐)
    lv_obj_align_to(text_label, emoji_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    // 3.4 让这个文本框中的文字为黑色
    lv_obj_set_style_text_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    // 3.5 这个文本框中的文字全局居中
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // 3.6 挂在中文字库
    lv_obj_set_style_text_font(text_label, &font_puhui_20_4, LV_PART_MAIN);

    lvgl_port_unlock();
}

void aixiaozhi_display_tip(char *tip) // 显示提示信息
{
    lvgl_port_lock(0);
    lv_label_set_text(tip_label, tip);
    lvgl_port_unlock();
}

void aixiaozhi_display_text(char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(text_label, text); // 将文字打印在设置好的区域上
    lvgl_port_unlock();
}

void aixiaozhi_display_emoji(char *emoji_name)
{
    lvgl_port_lock(0);
    bool found = false;
    for (uint8_t i = 0; i < 21; i++)
    {
        if (strcmp(emoji_list[i].name, emoji_name) == 0)
        {
            lv_label_set_text(emoji_label, emoji_list[i].emoji);
            found = true;
            break;
        }
    }
    if (found == false)
    {
        lv_label_set_text(emoji_label, "😏");
    }

    lvgl_port_unlock();
}

void aixiaozhi_show_qrcode(const char *data, uint32_t data_len) // 显示二维码
{
    lvgl_port_lock(0);

    lv_color_t bg_color = lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 5);
    lv_color_t fg_color = lv_palette_darken(LV_PALETTE_BLUE, 4);

    qr = lv_qrcode_create(lv_screen_active());
    lv_qrcode_set_size(qr, 220);
    lv_qrcode_set_dark_color(qr, fg_color);
    lv_qrcode_set_light_color(qr, bg_color);

    /*Set data*/
    lv_qrcode_update(qr, data, data_len);
    lv_obj_center(qr);

    /*Add a border with bg_color*/
    lv_obj_set_style_border_color(qr, bg_color, 0);
    lv_obj_set_style_border_width(qr, 5, 0);

    lvgl_port_unlock();
}

void aixiaozhi_display_delete_qrcode(void) // 删除二维码
{
    if (qr == NULL)
    {
        return;
    }

    lvgl_port_lock(0);
    lv_obj_del(qr);
    qr = NULL;
    lvgl_port_unlock();
}
