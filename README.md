# AI 小智（aixiaozhi）

一个基于 **ESP32-S3** 与 **ESP-IDF** 的 AI 语音助手固件。设备通过 Wi-Fi 接入云端大模型服务，实现「唤醒 → 语音上传 → 云端识别与回复 → 本地播放」的完整语音对话闭环，并在屏幕上同步显示字幕、表情与配网二维码。

> 项目参考小智（xiaozhi）开源生态，硬件型号标识为 `bread-compact-wifi-128x64/1.0.1`。

---

## ✨ 功能特性

| 功能 | 说明 |
| --- | --- |
| 🎙️ 语音唤醒 | 基于 `esp-sr` 音频前端（AFE）+ WakeNet 唤醒词检测，灵敏度可调 |
| 🔊 语音对话 | Opus 编码上行 / 解码下行，16 kHz 单声道，60 ms 帧长 |
| 🧠 云端大模型 | 通过 WebSocket 与云端服务实时双向通信，支持流式 TTS |
| 📺 屏幕显示 | LVGL 驱动 LCD，显示对话字幕、表情（emoji）、提示文字 |
| 📶 BLE 配网 | 蓝牙配网 + 屏幕二维码，扫码即可完成 Wi-Fi 配置 |
| 🔘 按键交互 | 前按键单击重启/唤醒，后按键长按重置 Wi-Fi |
| 🏠 IoT 控制 | 接收云端 IoT 指令，支持 `SetVolume`、`SetMute` 等设备控制 |
| 🔑 设备激活 | HTTP 请求云端激活接口，未激活时屏幕显示激活码 |
| 🛡️ 回声抑制 | 自研状态机避免设备听到自己播放的声音（防自问自答） |

---

## 🧩 硬件要求

- **主控**：ESP32-S3（需外挂 PSRAM，本项目使用 QUAD PSRAM）
- **Flash**：8 MB（分支分区表见 `partitions.csv`，应用分区 6 MB + 模型分区 300 KB）
- **音频**：I2S 接口的麦克风 + 扬声器（ES7210 / ES8311 类 codec）
- **显示**：SPI LCD 屏幕（128×64 或兼容分辨率）
- **按键**：前按键 / 后按键共 2 路
- **开发环境**：ESP-IDF **v5.4.4**（`C:\esp\v5.4.4\esp-idf`），VSCode + ESP-IDF 插件

---

## 📁 目录结构

```
aixiaozhi/
├── CMakeLists.txt              # 顶层 CMake，工程名 aixiaozhi
├── partitions.csv              # 分区表：nvs / otadata / phy_init / factory / model
├── dependencies.lock           # 组件管理器依赖锁定文件
├── .devcontainer/              # ESP-IDF QEMU 开发容器
├── .vscode/                    # VSCode 配置（目标芯片、串口、OpenOCD）
└── main/
    ├── main.c                  # 入口：app_main、业务状态机、WebSocket 业务回调
    ├── CMakeLists.txt          # main 组件源文件与头文件路径
    ├── idf_component.yml       # 依赖清单（button/qrcode/lvgl_port/esp-sr/...）
    ├── audio/                  # 音频子系统
    │   ├── aixiaozhi_codec.c         # I2C + I2S codec 初始化、播放/录音、音量/静音
    │   ├── aixiaozhi_opus_encoder.c  # Opus 编码（上行）
    │   ├── aixiaozhi_opus_decoder.c  # Opus 解码（下行）
    │   └── aixiaozhi_sr.c            # esp-sr 音频前端：唤醒词 + VAD
    ├── button/                 # 按键（iot_button）
    ├── display/                # 显示子系统（LVGL + LCD）
    ├── internet/               # 网络子系统
    │   ├── aixiaozhi_wifi.c          # Wi-Fi 连接与 BLE 配网
    │   ├── aixiaozhi_active.c        # 设备激活（HTTP → api.tenclass.net）
    │   └── aixiaozhi_ws.c            # WebSocket 客户端与协议收发
    ├── common/                 # 公共类型与全局句柄定义
    └── pre_data/               # 协议 JSON 模板（hello / iot_desc / iot_state ...）
```

---

## 🏗️ 系统架构

```
        ┌──────────────┐   I2S    ┌────────────────────┐   Opus    ┌───────────────┐
  MIC → │   Codec 采集  │ ───────→ │  esp-sr AFE(唤醒/VAD) │ ────────→ │  Opus 编码器   │
        └──────────────┘          └────────────────────┘           └───────┬───────┘
                                                                            │
                                                                     ┌──────▼───────┐
                                                                     │  环形缓冲区   │
                                                                     └──────┬───────┘
                                                                            │ 二进制帧
                                                              ┌─────────────▼─────────────┐
                                                              │      WebSocket 客户端      │◄──► 云端服务
                                                              └─────────────┬─────────────┘
                                                                            │ 二进制帧
                                                                     ┌──────▼───────┐
                                                                     │  环形缓冲区   │
                                                                     └──────┬───────┘
                                                                            │
                                                              ┌─────────────▼─────────────┐
  SPK ← Codec 播放 ← Opus 解码器 ←────────────────────────────────┤  LVGL 显示字幕/表情/二维码 │
                                                              └───────────────────────────┘
```

### 业务状态机

设备在三种状态间切换，由 `common_types.h` 中的 `aixiaozhi_status_enum` 定义：

- `AIXIAOZHI_STATUS_IDLE` — 空闲，等待唤醒
- `AIXIAOZHI_STATUS_LISTENING` — 正在采集并上传用户语音
- `AIXIAOZHI_STATUS_SPEAKING` — 正在播放云端 TTS 语音

VAD 状态变化驱动状态流转；在 `SPEAKING` 状态下若检测到人声，认为是设备自身播放的声音而不会打断，从而实现自研回声抑制。

---

## 🔌 通信协议要点

WebSocket 文本帧为 JSON，主要 `type`：

- `hello` — 握手，含版本、传输方式、音频参数（opus / 16000 Hz / 单声道 / 60 ms）
- `tts` — TTS 状态机（`start` / `sentence_start` / `stop`），`sentence_start` 携带字幕文本
- `llm` — 携带 `emotion` 字段，用于屏幕表情切换
- `iot` — 云端下发的设备控制指令（如 `SetVolume`、`SetMute`）

二进制帧为 Opus 音频数据，上行（麦克风）与下行（扬声器）分离处理。

---

## 🛠️ 构建与烧录

### 1. 环境准备

安装 **ESP-IDF v5.4.4** 并执行导出脚本：

```bash
# Windows (PowerShell)
C:\esp\v5.4.4\esp-idf\export.ps1

# Linux / macOS
. $IDF_PATH/export.sh
```

### 2. 编译

```bash
idf.py set-target esp32s3
idf.py build
```

### 3. 烧录并监视串口

```bash
# Windows 下串口形如 COM12，按实际情况修改
idf.py -p COM12 flash monitor
```

退出监视：`Ctrl + ]`。

> 首次构建时，组件管理器会根据 `idf_component.yml` 自动下载 `managed_components/` 下的依赖。

---

## 📦 主要依赖

由 ESP-IDF 组件管理器（`idf_component.yml` / `dependencies.lock`）管理：

| 组件 | 用途 |
| --- | --- |
| `espressif/esp-sr` | 唤醒词识别与 VAD 音频前端 |
| `espressif/esp_audio_codec` | Opus 编解码 |
| `espressif/esp_codec_dev` | 音频 codec 设备驱动 |
| `espressif/esp_websocket_client` | WebSocket 客户端 |
| `espressif/esp_lvgl_port` + `lvgl/lvgl` | 图形界面 |
| `espressif/button` | 按键事件管理 |
| `espressif/qrcode` | 配网二维码生成 |
| `78/xiaozhi-fonts` | 中文字体 |
| `espressif/cjson` | JSON 解析 |

---

## 🚀 使用流程

1. 上电后设备进入 **BLE 配网**，屏幕显示配网二维码。
2. 手机扫码完成 Wi-Fi 配置；配网成功后自动删除二维码。
3. 设备向云端请求 **激活**；若未激活，屏幕显示激活码，需前往官网控制台激活。
4. 激活后单击 **前按键** 重启设备，进入待机。
5. 说出 **唤醒词** 唤醒，即可开始语音对话（屏幕同步显示字幕与表情）。
6. 长按 **后按键** 可重置 Wi-Fi 重新配网。

---

## ⚙️ 可调参数

在 `main/audio/aixiaozhi_sr.c` 中可调整音频前端行为：

- `afe_config->vad_min_noise_ms` — VAD 最小静音时长（默认 500 ms）
- `afe_config->vad_min_speech_ms` — VAD 最小语音时长（默认 128 ms）
- `afe_config->vad_mode` — VAD 灵敏度模式
- `afe_config->wakenet_mode` — 唤醒灵敏度（默认 `DET_MODE_95`）
- `afe_config->aec_init / se_init / ns_init` — 回声消除 / 人声增强 / 降噪开关

分区的 Flash 大小与模型分区可在 `partitions.csv` 中调整。

---

## 📄 许可证

本项目未附许可证文件。如需开源，请在仓库中补充 `LICENSE`（如 MIT / Apache-2.0）。
