#include "rs485_transport.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "rs485_transport";

static void transport_lock(rs485_transport_t *transport)
{
    if ((transport != NULL) && (transport->lock != NULL)) {
        xSemaphoreTake((SemaphoreHandle_t)transport->lock, pdMS_TO_TICKS(100));
    }
}

static void transport_unlock(rs485_transport_t *transport)
{
    if ((transport != NULL) && (transport->lock != NULL)) {
        xSemaphoreGive((SemaphoreHandle_t)transport->lock);
    }
}

static void set_rx_mode(const rs485_transport_t *transport)
{
    if ((transport != NULL) && (transport->config.en_pin >= 0)) {
        gpio_set_level((gpio_num_t)transport->config.en_pin, 0);
    }
}

static void set_tx_mode(const rs485_transport_t *transport)
{
    if ((transport != NULL) && (transport->config.en_pin >= 0)) {
        gpio_set_level((gpio_num_t)transport->config.en_pin, 1);
    }
}

bool rs485_transport_init(rs485_transport_t *transport, const rs485_transport_config_t *config)
{
    if ((transport == NULL) || (config == NULL) || (config->tx_pin < 0) || (config->rx_pin < 0) || (config->en_pin < 0)) {
        return false;
    }

    memset(transport, 0, sizeof(*transport));
    transport->lock = xSemaphoreCreateMutex();
    if (transport->lock == NULL) {
        return false;
    }

    transport->config = *config;

    const uart_config_t uart_config = {
        .baud_rate = (int)config->baud_rate,
        .data_bits = config->data_bits,
        .parity = config->parity,
        .stop_bits = config->stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(config->uart_port, 512, 0, 0, NULL, 0) != ESP_OK) {
        vSemaphoreDelete((SemaphoreHandle_t)transport->lock);
        transport->lock = NULL;
        return false;
    }

    if ((uart_param_config(config->uart_port, &uart_config) != ESP_OK) ||
        (uart_set_pin(config->uart_port, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)) {
        uart_driver_delete(config->uart_port);
        vSemaphoreDelete((SemaphoreHandle_t)transport->lock);
        transport->lock = NULL;
        return false;
    }

    const gpio_config_t gpio_config_desc = {
        .pin_bit_mask = (1ULL << config->en_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&gpio_config_desc) != ESP_OK) {
        uart_driver_delete(config->uart_port);
        vSemaphoreDelete((SemaphoreHandle_t)transport->lock);
        transport->lock = NULL;
        return false;
    }

    set_rx_mode(transport);
    uart_flush_input(config->uart_port);
    transport->installed = true;
    transport->last_status = RS485_TRANSPORT_OK;
    return true;
}

void rs485_transport_deinit(rs485_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }

    if (transport->installed) {
        set_rx_mode(transport);
        uart_driver_delete(transport->config.uart_port);
        transport->installed = false;
    }
    if (transport->lock != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)transport->lock);
        transport->lock = NULL;
    }
}

bool rs485_transport_reconfigure(rs485_transport_t *transport, const rs485_transport_config_t *config)
{
    if ((transport == NULL) || (config == NULL)) {
        return false;
    }

    rs485_transport_deinit(transport);
    return rs485_transport_init(transport, config);
}

rs485_transport_status_t rs485_transport_send_frame(rs485_transport_t *transport, const uint8_t *data, size_t length)
{
    if ((transport == NULL) || !transport->installed || (data == NULL) || (length == 0)) {
        return RS485_TRANSPORT_INVALID_STATE;
    }

    transport_lock(transport);
    uart_flush_input(transport->config.uart_port);
    set_tx_mode(transport);
    if (transport->config.tx_pre_delay_us > 0) {
        esp_rom_delay_us(transport->config.tx_pre_delay_us);
    }

    const int written = uart_write_bytes(transport->config.uart_port, data, length);
    if (written != (int)length) {
        set_rx_mode(transport);
        transport_unlock(transport);
        transport->last_status = RS485_TRANSPORT_UART_ERROR;
        return transport->last_status;
    }

    if (uart_wait_tx_done(transport->config.uart_port, pdMS_TO_TICKS(transport->config.request_timeout_ms)) != ESP_OK) {
        set_rx_mode(transport);
        transport_unlock(transport);
        transport->last_status = RS485_TRANSPORT_TIMEOUT;
        return transport->last_status;
    }

    if (transport->config.tx_post_delay_us > 0) {
        esp_rom_delay_us(transport->config.tx_post_delay_us);
    }
    set_rx_mode(transport);
    transport_unlock(transport);
    transport->last_status = RS485_TRANSPORT_OK;
    return transport->last_status;
}

rs485_transport_status_t rs485_transport_receive_frame(rs485_transport_t *transport,
                                                       uint8_t *buffer,
                                                       size_t capacity,
                                                       size_t *received_length,
                                                       uint32_t overall_timeout_ms)
{
    if (received_length != NULL) {
        *received_length = 0;
    }
    if ((transport == NULL) || !transport->installed || (buffer == NULL) || (capacity == 0) || (received_length == NULL)) {
        return RS485_TRANSPORT_INVALID_ARG;
    }

    transport_lock(transport);
    const int64_t deadline = esp_timer_get_time() + ((int64_t)overall_timeout_ms * 1000LL);
    int64_t last_byte_time = 0;
    bool started = false;

    while (esp_timer_get_time() < deadline) {
        const uint32_t wait_ms = started ? transport->config.inter_frame_timeout_ms : 10;
        uint8_t byte = 0;
        const int read = uart_read_bytes(transport->config.uart_port, &byte, 1, pdMS_TO_TICKS(wait_ms));
        if (read > 0) {
            if (*received_length >= capacity) {
                transport_unlock(transport);
                transport->last_status = RS485_TRANSPORT_MALFORMED_FRAME;
                return transport->last_status;
            }

            buffer[(*received_length)++] = byte;
            started = true;
            last_byte_time = esp_timer_get_time();
            continue;
        }

        if (started && ((esp_timer_get_time() - last_byte_time) >= ((int64_t)transport->config.inter_frame_timeout_ms * 1000LL))) {
            break;
        }
    }
    transport_unlock(transport);

    transport->last_status = (*received_length == 0) ? RS485_TRANSPORT_TIMEOUT : RS485_TRANSPORT_OK;
    return transport->last_status;
}

rs485_transport_status_t rs485_transport_transact(rs485_transport_t *transport,
                                                  const uint8_t *tx_data,
                                                  size_t tx_length,
                                                  uint8_t *rx_buffer,
                                                  size_t rx_capacity,
                                                  size_t *rx_length,
                                                  uint32_t timeout_ms)
{
    if (rx_length != NULL) {
        *rx_length = 0;
    }
    if ((transport == NULL) || !transport->installed) {
        return RS485_TRANSPORT_INVALID_STATE;
    }

    const int64_t start = esp_timer_get_time();
    const rs485_transport_status_t send_status = rs485_transport_send_frame(transport, tx_data, tx_length);
    if (send_status != RS485_TRANSPORT_OK) {
        return send_status;
    }

    const rs485_transport_status_t receive_status = rs485_transport_receive_frame(transport,
                                                                                  rx_buffer,
                                                                                  rx_capacity,
                                                                                  rx_length,
                                                                                  timeout_ms);
    transport->last_round_trip_ms = (uint32_t)((esp_timer_get_time() - start) / 1000LL);
    return receive_status;
}

const char *rs485_transport_status_to_string(rs485_transport_status_t status)
{
    switch (status) {
    case RS485_TRANSPORT_OK:
        return "OK";
    case RS485_TRANSPORT_TIMEOUT:
        return "Timeout";
    case RS485_TRANSPORT_UART_ERROR:
        return "UART error";
    case RS485_TRANSPORT_INVALID_ARG:
        return "Invalid arg";
    case RS485_TRANSPORT_INVALID_STATE:
        return "Invalid state";
    case RS485_TRANSPORT_BUSY:
        return "Busy";
    case RS485_TRANSPORT_COLLISION:
        return "Collision";
    case RS485_TRANSPORT_MALFORMED_FRAME:
        return "Malformed frame";
    default:
        ESP_LOGW(TAG, "Unknown transport status %d", (int)status);
        return "Unknown";
    }
}