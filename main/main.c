/*
 * ESP32-S3 Wi-Fi CSI collection firmware.
 *
 * Output format:
 * CSI_DATA,node_id,seq,local_time_us,rssi,channel,secondary_channel,rate,
 * sig_mode,mcs,cwb,stbc,sgi,noise_floor,ant,sig_len,rx_state,csi_len,csi_bytes...
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAXIMUM_RETRY 10

static const char *TAG = "csi_collection";
static EventGroupHandle_t wifi_event_group;
static int retry_count;
static uint32_t csi_sequence;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (retry_count < WIFI_MAXIMUM_RETRY) {
      retry_count++;
      ESP_LOGW(TAG, "Wi-Fi disconnected, retrying (%d/%d)", retry_count,
               WIFI_MAXIMUM_RETRY);
      esp_wifi_connect();
    } else {
      xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    retry_count = 0;
    ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

static esp_err_t wifi_init_sta(void) {
  wifi_event_group = xEventGroupCreate();
  ESP_RETURN_ON_FALSE(wifi_event_group != NULL, ESP_ERR_NO_MEM, TAG,
                      "Failed to create Wi-Fi event group");

  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                      "event loop init failed");
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                          WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler,
                          NULL, NULL),
                      TAG, "register Wi-Fi event handler failed");
  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                          IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler,
                          NULL, NULL),
                      TAG, "register IP event handler failed");

  wifi_config_t wifi_config = {0};
  strlcpy((char *)wifi_config.sta.ssid, CONFIG_CSI_WIFI_SSID,
          sizeof(wifi_config.sta.ssid));
  strlcpy((char *)wifi_config.sta.password, CONFIG_CSI_WIFI_PASSWORD,
          sizeof(wifi_config.sta.password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                      "set Wi-Fi mode failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG,
                      "set Wi-Fi config failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG,
                      "disable Wi-Fi power save failed");

  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
  ESP_LOGI(TAG, "Connecting to SSID: %s", CONFIG_CSI_WIFI_SSID);

  EventBits_t bits = xEventGroupWaitBits(
      wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
      portMAX_DELAY);

  return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

static void csi_rx_callback(void *ctx, wifi_csi_info_t *info) {
  (void)ctx;
  if (info == NULL || info->buf == NULL) {
    return;
  }

  const wifi_pkt_rx_ctrl_t *rx = &info->rx_ctrl;
  const int64_t now_us = esp_timer_get_time();
  const uint32_t seq = csi_sequence++;

  printf("CSI_DATA,%d,%" PRIu32 ",%" PRId64
         ",%d,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u,%u",
         CONFIG_CSI_NODE_ID, seq, now_us, rx->rssi, rx->channel,
         rx->secondary_channel, rx->rate, rx->sig_mode, rx->mcs, rx->cwb,
         rx->stbc, rx->sgi, rx->noise_floor, rx->ant, rx->sig_len,
         rx->rx_state, info->len);

#if CONFIG_CSI_PRINT_RAW_IQ
  for (int i = 0; i < info->len; i++) {
    printf(",%d", info->buf[i]);
  }
#endif

  printf("\n");
}

static esp_err_t csi_init(void) {
  wifi_csi_config_t csi_config = {
      .lltf_en = false,
      .htltf_en = true,
      .stbc_htltf2_en = false,
      .ltf_merge_en = false,
      .channel_filter_en = false,
      .manu_scale = false,
      .shift = 0,
  };

  ESP_RETURN_ON_ERROR(esp_wifi_set_csi_config(&csi_config), TAG,
                      "set CSI config failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_csi_rx_cb(csi_rx_callback, NULL), TAG,
                      "set CSI callback failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_csi(true), TAG, "enable CSI failed");

  ESP_LOGI(TAG, "CSI enabled");
  ESP_LOGI(TAG,
           "CSI_DATA,node_id,seq,local_time_us,rssi,channel,secondary_channel,"
           "rate,sig_mode,mcs,cwb,stbc,sgi,noise_floor,ant,sig_len,rx_state,"
           "csi_len,csi_bytes...");
  return ESP_OK;
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESP32-S3 CSI collection starting");
  ESP_LOGI(TAG, "Node ID: %d", CONFIG_CSI_NODE_ID);

  if (strlen(CONFIG_CSI_WIFI_SSID) == 0) {
    ESP_LOGE(TAG, "Wi-Fi SSID is empty. Run `idf.py menuconfig` first.");
    return;
  }

  ESP_ERROR_CHECK(wifi_init_sta());
  ESP_ERROR_CHECK(csi_init());

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
