#include "sdkconfig.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE
#include "esp_coexist.h"
#endif

#include "esp_hosted_coprocessor.h"

#if CONFIG_ZB_ENABLED
#include "c6_zigbee_service.h"
#endif

static const char *TAG = "c6-app-main";

static esp_err_t init_nvs_storage(void)
{
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs_storage());
    ESP_ERROR_CHECK(esp_hosted_coprocessor_init());

#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_ZB_ENABLED
    ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());
#endif

#if CONFIG_ZB_ENABLED
    ESP_ERROR_CHECK(c6_zigbee_start());
#else
    ESP_LOGI(TAG, "ZigBee is disabled. Hosted Wi-Fi and BLE coprocessor services are active.");
#endif
}