#include "sdkconfig.h"

#if CONFIG_ZB_ENABLED

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "c6_zigbee_service.h"

static const char *TAG = "c6-zigbee";

static const uint32_t kPrimaryChannelMask = (1UL << 13);
static const uint8_t kGatewayEndpoint = 1;
static const uint8_t kMaxChildren = 10;
static const bool kInstallCodePolicy = false;
static const char kManufacturerName[] = "\x09" "ESPRESSIF";
static const char kModelIdentifier[] = "\x07" "ESP32C6";

static void startCommissioning(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *signal = signal_struct->p_app_signal;
    esp_err_t status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t signal_type = *signal;

    switch (signal_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Initializing ZigBee stack");
            startCommissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (status == ESP_OK) {
                if (esp_zb_bdb_is_factory_new()) {
                    ESP_LOGI(TAG, "Starting ZigBee network formation");
                    startCommissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
                } else {
                    ESP_LOGI(TAG, "Reopened existing ZigBee network");
                    esp_zb_bdb_open_network(180);
                }
            } else {
                ESP_LOGE(TAG, "ZigBee startup failed: %s", esp_err_to_name(status));
            }
            break;

        case ESP_ZB_BDB_SIGNAL_FORMATION:
            if (status == ESP_OK) {
                ESP_LOGI(TAG, "ZigBee network formed on channel %d, PAN ID 0x%04x",
                         esp_zb_get_current_channel(), esp_zb_get_pan_id());
                startCommissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGW(TAG, "ZigBee formation retry after status: %s", esp_err_to_name(status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)startCommissioning,
                                       ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
            }
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (status == ESP_OK) {
                ESP_LOGI(TAG, "ZigBee network steering started");
            }
            break;

        default:
            ESP_LOGI(TAG, "ZigBee signal %s (0x%x), status: %s",
                     esp_zb_zdo_signal_to_string(signal_type), signal_type, esp_err_to_name(status));
            break;
    }
}

static void zigbeeTask(void *arg)
{
    (void)arg;

    esp_zb_cfg_t config = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = kInstallCodePolicy,
        .nwk_cfg.zczr_cfg = {
            .max_children = kMaxChildren,
        },
    };

    esp_zb_init(&config);
    esp_zb_set_primary_network_channel_set(kPrimaryChannelMask);

    esp_zb_ep_list_t *endpoint_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = kGatewayEndpoint,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, kManufacturerName);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, kModelIdentifier);

    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(NULL),
                                             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_ep_list_add_gateway_ep(endpoint_list, cluster_list, endpoint_config);
    esp_zb_device_register(endpoint_list);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
    vTaskDelete(NULL);
}

esp_err_t c6_zigbee_start(void)
{
    static bool started = false;
    if (started) {
        return ESP_OK;
    }

    esp_zb_platform_config_t platform_config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };


    ESP_RETURN_ON_ERROR(esp_zb_platform_config(&platform_config), TAG, "Failed to configure ZigBee platform");

    BaseType_t task_ok = xTaskCreate(zigbeeTask, "zigbee_main", 8192, NULL, 5, NULL);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to start ZigBee task");

    started = true;
    return ESP_OK;
}

#endif