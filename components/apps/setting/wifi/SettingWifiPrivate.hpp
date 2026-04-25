#pragma once

#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "lvgl.h"

#include "../Setting.hpp"

#define ENABLE_DEBUG_LOG                (0)

#define WIFI_SCAN_TASK_STACK_SIZE       (1024 * 6)
#define WIFI_SCAN_TASK_PRIORITY         (1)
#define WIFI_SCAN_TASK_PERIOD_MS        (5 * 1000)

#define WIFI_CONNECT_TASK_STACK_SIZE    (1024 * 4)
#define WIFI_CONNECT_TASK_PRIORITY      (4)
#define WIFI_CONNECT_TASK_STACK_CORE    (0)
#define WIFI_CONNECT_UI_WAIT_TIME_MS    (1 * 1000)
#define WIFI_CONNECT_UI_PANEL_SIZE      (1 * 1000)
#define WIFI_CONNECT_RET_WAIT_TIME_MS   (10 * 1000)
#define WIFI_RECONNECT_RETRY_PERIOD_MS  (5 * 1000)

#define NVS_KEY_WIFI_ENABLE             "wifi_en"
#define NVS_KEY_WIFI_AP_ENABLE          "wifi_ap_en"
#define NVS_KEY_WIFI_SSID               "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD           "wifi_pass"
#define NVS_KEY_WIFI_AP_SSID            "wifi_ap_ssid"
#define NVS_KEY_WIFI_AP_PASSWORD        "wifi_ap_pass"
#define NVS_KEY_WIFI_NETWORKS           "wifi_nets"
#define NVS_KEY_DISPLAY_TZ_AUTO         "disp_tz_auto"

#define UI_WIFI_LIST_UP_OFFSET          (20)
#define UI_WIFI_LIST_ITEM_H             (60)
#define UI_WIFI_LIST_ITEM_FONT          (&lv_font_montserrat_26)
#define UI_WIFI_ICON_LOCK_RIGHT_OFFSET       (-10)
#define UI_WIFI_ICON_SIGNAL_RIGHT_OFFSET     (-50)
#define UI_WIFI_ICON_CONNECT_RIGHT_OFFSET    (-90)

#define WIFI_SSID_STORAGE_SIZE      32
#define WIFI_PASSWORD_STORAGE_SIZE  64
#define WIFI_AP_SSID_STORAGE_SIZE   33
#define WIFI_AP_PASSWORD_STORAGE_SIZE 65
#define SCAN_LIST_SIZE      25
#define WIFI_SAVED_NETWORK_LIMIT 20

extern EventGroupHandle_t s_wifi_event_group;
extern bool s_wifi_restore_in_progress;
extern bool s_wifi_runtime_ready;

extern char st_wifi_ssid[WIFI_SSID_STORAGE_SIZE];
extern char st_wifi_password[WIFI_PASSWORD_STORAGE_SIZE];
extern char st_wifi_ap_ssid[WIFI_AP_SSID_STORAGE_SIZE];
extern char st_wifi_ap_password[WIFI_AP_PASSWORD_STORAGE_SIZE];

extern lv_obj_t *panel_wifi_btn[SCAN_LIST_SIZE];
extern lv_obj_t *label_wifi_ssid[SCAN_LIST_SIZE];
extern lv_obj_t *img_img_wifi_lock[SCAN_LIST_SIZE];
extern lv_obj_t *wifi_image[SCAN_LIST_SIZE];
extern lv_obj_t *wifi_connect[SCAN_LIST_SIZE];

LV_IMG_DECLARE(img_wifisignal_absent);
LV_IMG_DECLARE(img_wifisignal_wake);
LV_IMG_DECLARE(img_wifisignal_moderate);
LV_IMG_DECLARE(img_wifisignal_good);
LV_IMG_DECLARE(img_wifi_lock);

typedef enum {
    WIFI_EVENT_CONNECTED = BIT(0),
    WIFI_EVENT_INIT_DONE = BIT(1),
    WIFI_EVENT_UI_INIT_DONE = BIT(2),
    WIFI_EVENT_SCANING = BIT(3),
    WIFI_EVENT_CONNECTING = BIT(4),
    WIFI_EVENT_SCAN_RUNNING = BIT(5)
} wifi_event_id_t;

bool lv_obj_ready(lv_obj_t *obj);

BaseType_t create_background_task_prefer_psram(TaskFunction_t task,
                                               const char *name,
                                               uint32_t stack_depth,
                                               void *arg,
                                               UBaseType_t priority,
                                               TaskHandle_t *task_handle,
                                               BaseType_t core_id);

std::string trim_copy(const std::string &text);