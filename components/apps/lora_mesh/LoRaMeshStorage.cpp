#include "LoRaMeshStorage.hpp"

#include <vector>

#include "LoRaMeshCrypto.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

namespace jc4880::lora_mesh {
namespace {

constexpr char kTag[] = "LoRaMeshStore";
constexpr char kNamespace[] = "lora_mesh";
constexpr char kStateKey[] = "state";
constexpr int8_t kAllowedGpios[] = {29, 30, 31, 33, 34, 35, 50, 51, 52};

std::string read_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && (item->valuestring != nullptr) ? std::string(item->valuestring) : std::string();
}

int read_int(const cJSON *object, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool read_bool(const cJSON *object, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

void add_string(cJSON *object, const char *key, const std::string &value)
{
    if (!value.empty()) {
        cJSON_AddStringToObject(object, key, value.c_str());
    }
}

PeerPresence parse_presence(const std::string &value)
{
    if (value == "online") {
        return PeerPresence::Online;
    }
    if (value == "offline") {
        return PeerPresence::Offline;
    }
    return PeerPresence::Unknown;
}

bool is_allowed_gpio(int8_t gpio)
{
    if (gpio < 0) {
        return true;
    }
    for (int8_t allowed : kAllowedGpios) {
        if (allowed == gpio) {
            return true;
        }
    }
    return false;
}

void apply_module_defaults(MeshSettings &settings)
{
    switch (settings.radio_module) {
        case RadioModule::E22_400M22S:
            settings.spi_miso_gpio = 33;
            settings.spi_mosi_gpio = 31;
            settings.spi_sck_gpio = 30;
            settings.spi_nss_gpio = 29;
            settings.busy_gpio = 51;
            settings.dio1_gpio = 50;
            settings.nrst_gpio = 52;
            settings.txen_gpio = 35;
            settings.rxen_gpio = 34;
            settings.uart_tx_gpio = 31;
            settings.uart_rx_gpio = 33;
            settings.mode0_gpio = 29;
            settings.mode1_gpio = 30;
            settings.aux_gpio = 50;
            break;
        case RadioModule::E22_400T22S:
            settings.spi_miso_gpio = 33;
            settings.spi_mosi_gpio = 31;
            settings.spi_sck_gpio = 30;
            settings.spi_nss_gpio = 29;
            settings.busy_gpio = 51;
            settings.dio1_gpio = 50;
            settings.nrst_gpio = 51;
            settings.txen_gpio = 35;
            settings.rxen_gpio = 34;
            settings.uart_tx_gpio = 31;
            settings.uart_rx_gpio = 33;
            settings.mode0_gpio = 29;
            settings.mode1_gpio = 30;
            settings.aux_gpio = 50;
            break;
        case RadioModule::E220_400T22D:
            settings.spi_miso_gpio = 33;
            settings.spi_mosi_gpio = 31;
            settings.spi_sck_gpio = 30;
            settings.spi_nss_gpio = 29;
            settings.busy_gpio = 51;
            settings.dio1_gpio = 50;
            settings.nrst_gpio = -1;
            settings.txen_gpio = 35;
            settings.rxen_gpio = 34;
            settings.uart_tx_gpio = 31;
            settings.uart_rx_gpio = 33;
            settings.mode0_gpio = 29;
            settings.mode1_gpio = 30;
            settings.aux_gpio = 50;
            break;
    }
}

bool heal_known_e22_swapped_spi_pins(MeshSettings &settings, const MeshSettings &defaults)
{
    if (settings.radio_module != RadioModule::E22_400M22S) {
        return false;
    }

    const bool swapped_spi_pair = (settings.spi_miso_gpio == defaults.spi_sck_gpio) &&
                                  (settings.spi_sck_gpio == defaults.spi_miso_gpio);
    const bool other_pins_match_defaults = (settings.spi_mosi_gpio == defaults.spi_mosi_gpio) &&
                                           (settings.spi_nss_gpio == defaults.spi_nss_gpio) &&
                                           (settings.busy_gpio == defaults.busy_gpio) &&
                                           (settings.dio1_gpio == defaults.dio1_gpio) &&
                                           (settings.nrst_gpio == defaults.nrst_gpio) &&
                                           (settings.txen_gpio == defaults.txen_gpio) &&
                                           (settings.rxen_gpio == defaults.rxen_gpio);
    if (!swapped_spi_pair || !other_pins_match_defaults) {
        return false;
    }

    ESP_LOGW(kTag,
             "Correcting stored E22 SPI pin override MISO=%d/SCK=%d to defaults MISO=%d/SCK=%d",
             settings.spi_miso_gpio,
             settings.spi_sck_gpio,
             defaults.spi_miso_gpio,
             defaults.spi_sck_gpio);
    settings.spi_miso_gpio = defaults.spi_miso_gpio;
    settings.spi_sck_gpio = defaults.spi_sck_gpio;
    return true;
}

bool sanitize_module_pins(MeshSettings &settings)
{
    MeshSettings defaults = settings;
    apply_module_defaults(defaults);
    bool changed = heal_known_e22_swapped_spi_pins(settings, defaults);

    auto sanitize_pin = [&changed](int8_t &pin, int8_t fallback) {
        if (!is_allowed_gpio(pin)) {
            pin = fallback;
            changed = true;
        }
    };

    sanitize_pin(settings.spi_miso_gpio, defaults.spi_miso_gpio);
    sanitize_pin(settings.spi_mosi_gpio, defaults.spi_mosi_gpio);
    sanitize_pin(settings.spi_sck_gpio, defaults.spi_sck_gpio);
    sanitize_pin(settings.spi_nss_gpio, defaults.spi_nss_gpio);
    sanitize_pin(settings.busy_gpio, defaults.busy_gpio);
    sanitize_pin(settings.dio1_gpio, defaults.dio1_gpio);
    sanitize_pin(settings.nrst_gpio, defaults.nrst_gpio);
    sanitize_pin(settings.txen_gpio, defaults.txen_gpio);
    sanitize_pin(settings.rxen_gpio, defaults.rxen_gpio);
    sanitize_pin(settings.uart_tx_gpio, defaults.uart_tx_gpio);
    sanitize_pin(settings.uart_rx_gpio, defaults.uart_rx_gpio);
    sanitize_pin(settings.mode0_gpio, defaults.mode0_gpio);
    sanitize_pin(settings.mode1_gpio, defaults.mode1_gpio);
    sanitize_pin(settings.aux_gpio, defaults.aux_gpio);
    return changed;
}

bool load_state_json(std::string &json)
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    size_t required_size = 0;
    esp_err_t err = nvs_get_str(handle, kStateKey, nullptr, &required_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    std::vector<char> buffer(required_size, '\0');
    err = nvs_get_str(handle, kStateKey, buffer.data(), &required_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }

    json.assign(buffer.data());
    return !json.empty();
}

bool save_state_json(const std::string &json)
{
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_str(handle, kStateKey, json.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

bool sanitize_state(StoredState &state)
{
    bool changed = false;
    if (state.identity.public_key_hex.empty() || state.identity.private_key_hex.empty() || state.identity.device_id.empty()) {
        state.identity = cryptoGenerateIdentity();
        changed = true;
    }
    if (state.identity.display_name.empty()) {
        state.identity.display_name = std::string("P4-") + state.identity.device_id.substr(0, 4);
        changed = true;
    }
    if (state.settings.common_chat_name.empty()) {
        state.settings.common_chat_name = "Common Mesh Chat";
        changed = true;
    }
    if (state.settings.hop_limit == 0U) {
        state.settings.hop_limit = 4;
        changed = true;
    }
    if ((state.settings.spreading_factor < 5U) || (state.settings.spreading_factor > 12U)) {
        state.settings.spreading_factor = 9;
        changed = true;
    }
    if (state.settings.bandwidth > 9U) {
        state.settings.bandwidth = 4;
        changed = true;
    }
    if ((state.settings.coding_rate < 1U) || (state.settings.coding_rate > 4U)) {
        state.settings.coding_rate = 1;
        changed = true;
    }
    if (!state.settings.radio_enabled && state.settings.antenna_warning_acknowledged) {
        // Keep the disable state but do not treat the warning ack as invalid.
    }
    if (static_cast<uint8_t>(state.settings.radio_module) > static_cast<uint8_t>(RadioModule::E220_400T22D)) {
        state.settings.radio_module = RadioModule::E22_400M22S;
        changed = true;
    }
    changed = sanitize_module_pins(state.settings) || changed;
    if (state.settings.public_group_key_hex.empty()) {
        state.settings.public_group_key_hex = cryptoGenerateSharedSecretHex();
        changed = true;
    }
    return changed;
}

} // namespace

bool load_stored_state(StoredState &state)
{
    std::string json;
    if (!load_state_json(json)) {
        sanitize_state(state);
        return save_stored_state(state);
    }

    cJSON *root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        sanitize_state(state);
        return save_stored_state(state);
    }

    const cJSON *identity = cJSON_GetObjectItemCaseSensitive(root, "identity");
    state.identity.device_id = read_string(identity, "device_id");
    state.identity.display_name = read_string(identity, "display_name");
    state.identity.public_key_hex = read_string(identity, "public_key");
    state.identity.private_key_hex = read_string(identity, "private_key");

    const cJSON *settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
    state.settings.frequency_hz = static_cast<uint32_t>(read_int(settings, "frequency_hz", static_cast<int>(state.settings.frequency_hz)));
    state.settings.spreading_factor = static_cast<uint8_t>(read_int(settings, "spreading_factor", state.settings.spreading_factor));
    state.settings.bandwidth = static_cast<uint8_t>(read_int(settings, "bandwidth", state.settings.bandwidth));
    state.settings.coding_rate = static_cast<uint8_t>(read_int(settings, "coding_rate", state.settings.coding_rate));
    state.settings.radio_enabled = read_bool(settings, "radio_enabled", state.settings.radio_enabled);
    state.settings.radio_module = static_cast<RadioModule>(read_int(settings, "radio_module", static_cast<int>(state.settings.radio_module)));
    state.settings.spi_miso_gpio = static_cast<int8_t>(read_int(settings, "spi_miso_gpio", state.settings.spi_miso_gpio));
    state.settings.spi_mosi_gpio = static_cast<int8_t>(read_int(settings, "spi_mosi_gpio", state.settings.spi_mosi_gpio));
    state.settings.spi_sck_gpio = static_cast<int8_t>(read_int(settings, "spi_sck_gpio", state.settings.spi_sck_gpio));
    state.settings.spi_nss_gpio = static_cast<int8_t>(read_int(settings, "spi_nss_gpio", state.settings.spi_nss_gpio));
    state.settings.busy_gpio = static_cast<int8_t>(read_int(settings, "busy_gpio", state.settings.busy_gpio));
    state.settings.dio1_gpio = static_cast<int8_t>(read_int(settings, "dio1_gpio", state.settings.dio1_gpio));
    state.settings.nrst_gpio = static_cast<int8_t>(read_int(settings, "nrst_gpio", state.settings.nrst_gpio));
    state.settings.txen_gpio = static_cast<int8_t>(read_int(settings, "txen_gpio", state.settings.txen_gpio));
    state.settings.rxen_gpio = static_cast<int8_t>(read_int(settings, "rxen_gpio", state.settings.rxen_gpio));
    state.settings.uart_tx_gpio = static_cast<int8_t>(read_int(settings, "uart_tx_gpio", state.settings.uart_tx_gpio));
    state.settings.uart_rx_gpio = static_cast<int8_t>(read_int(settings, "uart_rx_gpio", state.settings.uart_rx_gpio));
    state.settings.mode0_gpio = static_cast<int8_t>(read_int(settings, "mode0_gpio", state.settings.mode0_gpio));
    state.settings.mode1_gpio = static_cast<int8_t>(read_int(settings, "mode1_gpio", state.settings.mode1_gpio));
    state.settings.aux_gpio = static_cast<int8_t>(read_int(settings, "aux_gpio", state.settings.aux_gpio));
    state.settings.public_chat_encryption = read_bool(settings, "public_chat_encryption", state.settings.public_chat_encryption);
    state.settings.common_chat_name = read_string(settings, "common_chat_name");
    state.settings.hop_limit = static_cast<uint8_t>(read_int(settings, "hop_limit", state.settings.hop_limit));
    state.settings.forwarding_enabled = read_bool(settings, "forwarding_enabled", state.settings.forwarding_enabled);
    state.settings.antenna_warning_acknowledged = read_bool(settings, "antenna_warning_acknowledged", state.settings.antenna_warning_acknowledged);
    state.settings.public_group_key_hex = read_string(settings, "public_group_key");

    state.peers.clear();
    const cJSON *peers = cJSON_GetObjectItemCaseSensitive(root, "peers");
    if (cJSON_IsArray(peers)) {
        const cJSON *peer = nullptr;
        cJSON_ArrayForEach(peer, peers) {
            PeerInfo entry = {};
            entry.device_id = read_string(peer, "device_id");
            if (entry.device_id.empty()) {
                continue;
            }
            entry.display_name = read_string(peer, "display_name");
            entry.public_key_hex = read_string(peer, "public_key");
            entry.pair_secret_hex = read_string(peer, "pair_secret");
            entry.last_seen_ms = static_cast<int64_t>(read_int(peer, "last_seen_ms", 0));
            entry.last_rssi = read_int(peer, "last_rssi", 0);
            entry.last_snr = read_int(peer, "last_snr", 0);
            entry.presence = parse_presence(read_string(peer, "presence"));
            state.peers.push_back(entry);
        }
    }
    cJSON_Delete(root);

    if (sanitize_state(state) && !save_stored_state(state)) {
        ESP_LOGW(kTag, "Failed to persist sanitized LoRa mesh state");
    }
    return true;
}

bool save_stored_state(const StoredState &state)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON *identity = cJSON_AddObjectToObject(root, "identity");
    add_string(identity, "device_id", state.identity.device_id);
    add_string(identity, "display_name", state.identity.display_name);
    add_string(identity, "public_key", state.identity.public_key_hex);
    add_string(identity, "private_key", state.identity.private_key_hex);

    cJSON *settings = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddNumberToObject(settings, "frequency_hz", static_cast<double>(state.settings.frequency_hz));
    cJSON_AddNumberToObject(settings, "spreading_factor", state.settings.spreading_factor);
    cJSON_AddNumberToObject(settings, "bandwidth", state.settings.bandwidth);
    cJSON_AddNumberToObject(settings, "coding_rate", state.settings.coding_rate);
    cJSON_AddBoolToObject(settings, "radio_enabled", state.settings.radio_enabled);
    cJSON_AddNumberToObject(settings, "radio_module", static_cast<int>(state.settings.radio_module));
    cJSON_AddNumberToObject(settings, "spi_miso_gpio", state.settings.spi_miso_gpio);
    cJSON_AddNumberToObject(settings, "spi_mosi_gpio", state.settings.spi_mosi_gpio);
    cJSON_AddNumberToObject(settings, "spi_sck_gpio", state.settings.spi_sck_gpio);
    cJSON_AddNumberToObject(settings, "spi_nss_gpio", state.settings.spi_nss_gpio);
    cJSON_AddNumberToObject(settings, "busy_gpio", state.settings.busy_gpio);
    cJSON_AddNumberToObject(settings, "dio1_gpio", state.settings.dio1_gpio);
    cJSON_AddNumberToObject(settings, "nrst_gpio", state.settings.nrst_gpio);
    cJSON_AddNumberToObject(settings, "txen_gpio", state.settings.txen_gpio);
    cJSON_AddNumberToObject(settings, "rxen_gpio", state.settings.rxen_gpio);
    cJSON_AddNumberToObject(settings, "uart_tx_gpio", state.settings.uart_tx_gpio);
    cJSON_AddNumberToObject(settings, "uart_rx_gpio", state.settings.uart_rx_gpio);
    cJSON_AddNumberToObject(settings, "mode0_gpio", state.settings.mode0_gpio);
    cJSON_AddNumberToObject(settings, "mode1_gpio", state.settings.mode1_gpio);
    cJSON_AddNumberToObject(settings, "aux_gpio", state.settings.aux_gpio);
    cJSON_AddBoolToObject(settings, "public_chat_encryption", state.settings.public_chat_encryption);
    add_string(settings, "common_chat_name", state.settings.common_chat_name);
    cJSON_AddNumberToObject(settings, "hop_limit", state.settings.hop_limit);
    cJSON_AddBoolToObject(settings, "forwarding_enabled", state.settings.forwarding_enabled);
    cJSON_AddBoolToObject(settings, "antenna_warning_acknowledged", state.settings.antenna_warning_acknowledged);
    add_string(settings, "public_group_key", state.settings.public_group_key_hex);

    cJSON *peers = cJSON_AddArrayToObject(root, "peers");
    for (const PeerInfo &peer : state.peers) {
        cJSON *entry = cJSON_CreateObject();
        if (entry == nullptr) {
            continue;
        }
        add_string(entry, "device_id", peer.device_id);
        add_string(entry, "display_name", peer.display_name);
        add_string(entry, "public_key", peer.public_key_hex);
        add_string(entry, "pair_secret", peer.pair_secret_hex);
        cJSON_AddNumberToObject(entry, "last_seen_ms", static_cast<double>(peer.last_seen_ms));
        cJSON_AddNumberToObject(entry, "last_rssi", peer.last_rssi);
        cJSON_AddNumberToObject(entry, "last_snr", peer.last_snr);
        add_string(entry, "presence", presence_name(peer.presence));
        cJSON_AddItemToArray(peers, entry);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == nullptr) {
        return false;
    }

    const std::string encoded(json);
    cJSON_free(json);
    if (!save_state_json(encoded)) {
        ESP_LOGW(kTag, "Failed to save LoRa mesh state");
        return false;
    }
    return true;
}

} // namespace jc4880::lora_mesh