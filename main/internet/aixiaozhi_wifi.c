#include "aixiaozhi_wifi.h"
#include "aixiaozhi_display.h"

#define TAG "aixiaozhi_wifi"

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT 1

#define PROV_QR_VERSION "v1"
#define QRCODE_BASE_URL "https://espressif.github.io/esp-jumpstart/qrcode.html"

void aixiaozhi_wifi_nvs_init(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase()); // 清除
    ret = nvs_flash_init();             // 重新初始化
  }
  ESP_ERROR_CHECK(ret);
}

static void get_device_service_name(char *service_name, size_t max)
{
  uint8_t eth_mac[6];
  const char *ssid_prefix = "PROV_";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(service_name, max, "%s%02X%02X%02X",
           ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

// 生成二维码
static void wifi_prov_show_qr(const char *name, const char *username, const char *pop, const char *transport)
{
  if (!name || !transport)
  {
    ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
    return;
  }
  char payload[150] = {0};
  if (pop)
  {
    snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\""
                                       ",\"pop\":\"%s\",\"transport\":\"%s\"}",
             PROV_QR_VERSION, name, pop, transport);
  }
  else
  {
    snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\""
                                       ",\"transport\":\"%s\"}",
             PROV_QR_VERSION, name, transport);
  }
  ESP_LOGI(TAG, "Scan this QR code from the provisioning application for Provisioning.");
  // esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  // esp_qrcode_generate(&cfg, payload);
  aixiaozhi_display_tip("请扫描二维码进行配网");
  aixiaozhi_show_qrcode(payload, strlen(payload));
  ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s", QRCODE_BASE_URL, payload);
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) // base部门，id事件
{
  if (event_base == WIFI_EVENT)
  {
    if (event_id == WIFI_EVENT_STA_START)
    {
      // 如果成功进入STA模式
      ESP_LOGI(TAG, "wifi已经进入STA模式，正在连接wifi...");

      esp_wifi_connect();
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
      ESP_LOGW(TAG, "wifi连接失败，正在重连...");
      esp_wifi_connect();
    }
  }

  if (event_base == IP_EVENT)
  {
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
      ESP_LOGI(TAG, "wifi连接成功");
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
  }

  if (event_base == WIFI_PROV_EVENT)
  {
    switch (event_id)
    {
    case WIFI_PROV_START: // 刚开始配网，只打日志
    {
      ESP_LOGI(TAG, "Provisioning started");
      break;
    }
    case WIFI_PROV_CRED_RECV: // 收到wifi名和密码，打日志
    {
      wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
      ESP_LOGI(TAG, "Received Wi-Fi credentials"
                    "\n\tSSID     : %s\n\tPassword : %s",
               (const char *)wifi_sta_cfg->ssid,
               (const char *)wifi_sta_cfg->password);
      break;
    }
    case WIFI_PROV_CRED_FAIL: // 配网失败，打日志，把配网管理器的状态重置
    {
      wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
      ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                    "\n\tPlease reset to factory and retry provisioning",
               (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
      // 如果配网失败了 就把配网管理器复位一下
      wifi_prov_mgr_reset_sm_state_on_failure();
      break;
    }
    case WIFI_PROV_CRED_SUCCESS: // 配网成功了 wifi名和密码是对的
    {
      ESP_LOGI(TAG, "Provisioning successful");
      break;
    }
    case WIFI_PROV_END: // 配网结束了 删除配网的任务 释放内存和资源
    {
      wifi_prov_mgr_deinit();
      break;
    }
    default:
      break;
    }
  }

  if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT)
  {
    switch (event_id)
    {
    case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
      ESP_LOGI(TAG, "BLE transport: Connected!");
      break;
    case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
      ESP_LOGI(TAG, "BLE transport: Disconnected!");
      break;
    default:
      break;
    }
  }

  if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT)
  {
    switch (event_id)
    {
    case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
      ESP_LOGI(TAG, "Secured session established!");
      break;
    case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
      ESP_LOGE(TAG, "Received invalid security parameters for establishing secure session!");
      break;
    case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
      ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing secure session!");
      break;
    default:
      break;
    }
  }
}

void aixiaozhi_wifi_init(void)
{
  aixiaozhi_wifi_nvs_init();
  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  s_wifi_event_group = xEventGroupCreate();

  // 创建网络任务
  ESP_ERROR_CHECK(esp_netif_init());
  // 创建默认的事件循环任务（freertos任务）
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // 创建一个wifi任务
  esp_netif_create_default_wifi_sta();                 // 建立通道
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // 初始化
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // 给事件循环注册回调函数，仅仅用作注销函数用
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  esp_event_handler_instance_t instance_wifi_prov;
  esp_event_handler_instance_t instance_ble_transport;
  esp_event_handler_instance_t instance_security_session;

  // 先注册这5个回调，判断base和id是否满足，然后当我后台处理配网信息，当这两个都满足了调用event_handler
  //  不管是wifi的什么事件都调用event_handler函数
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  // 仅成功拿到IP地址时调用event_handler函数
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
  // 注册wifi配网事件
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_wifi_prov));
  // 注册蓝牙传输事件
  ESP_ERROR_CHECK(esp_event_handler_instance_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_ble_transport));
  // 注册安全会话事件
  ESP_ERROR_CHECK(esp_event_handler_instance_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_security_session));

  // 配网管理器的配置
  wifi_prov_mgr_config_t config =
      {
          .wifi_prov_conn_cfg =
              {
                  .wifi_conn_attempts = 5,
              },
          .scheme = wifi_prov_scheme_ble,
          .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

  ESP_ERROR_CHECK(wifi_prov_mgr_init(config)); // 初始化配网管理器

  bool provisioned = false; // 判断有没有配过网

  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned)); // 执行是否配过网的检查

  if (!provisioned)
  {
    // 没有配过网

    ESP_LOGI(TAG, "Starting provisioning");
    // 定义一个设备名，蓝牙搜索时显示
    char service_name[32];
    get_device_service_name(service_name, sizeof(service_name));
    // 启用安全版本1
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    // 定义一个用以对称加密的pop值
    const char *pop = "abcd1234";
    // 把pop放到安全配置里面
    wifi_prov_security1_params_t *sec_params = pop;

    const char *usrname = NULL;
    // 给蓝牙的数据字段设置uuid，wifi专用
    uint8_t custom_service_uuid[] =
        {
            /* LSB <---------------------------------------
             * ---------------------------------------> MSB */
            0xb4,
            0xdf,
            0x5a,
            0x1c,
            0x3f,
            0x6b,
            0xf4,
            0xbf,
            0xea,
            0x4a,
            0x82,
            0x03,
            0x04,
            0x90,
            0x1a,
            0x02,
        };
    // 把uuid放到安全配置里面
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
    // 启动配网
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *)sec_params, service_name, NULL));
    // 打印二维码
    wifi_prov_show_qr(service_name, usrname, pop, "ble");
  }
  else
  {
    // 配过网
    ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
    // 反初始化配网管理器
    wifi_prov_mgr_deinit();
    // 进入STA模式
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
  }

  ESP_LOGI(TAG, "wifi_init_sta finished.");

  ESP_LOGI(TAG, "主程序 正在等待wifi连接");

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);

  ESP_LOGI(TAG, "整个wifi初始化流程 彻底结束了！而且已经有网络了！");
}

void aixiaozhi_wifi_reset(void)
{
  esp_wifi_restore();
  // 重启esp32单片机
  esp_restart();
}
