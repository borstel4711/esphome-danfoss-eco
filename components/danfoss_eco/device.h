#pragma once

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/climate/climate.h"

#include "esphome/core/preferences.h"

#include "helpers.h"
#include "command.h"
#include "properties.h"
#include "my_component.h"
#include "xxtea.h"

#ifdef USE_ESP32

#include <esp_gattc_api.h>
#include <set>

namespace esphome
{
  namespace danfoss_eco
  {
    using namespace std;
    using namespace climate;

    class Device : public MyComponent, public esphome::ble_client::BLEClientNode
    {
    public:
      Device() : xxtea(make_shared<Xxtea>()){};

      void dump_config() override
      {
        LOG_CLIMATE("", "Danfoss Eco eTRV", this);
        ESP_LOGCONFIG(TAG, "  MAC Address: %s", this->parent()->address_str().c_str());
        LOG_SENSOR("", "Battery Level", this->battery_level_);
        LOG_SENSOR("", "Room Temperature", this->temperature_);
        LOG_BINARY_SENSOR("", "Problems", this->problems_);
        LOG_TEXT_SENSOR("", "Problems (Detail)", this->problems_detail_);
      }

      void setup() override;
      void loop() override;
      void update() override;
      void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) override;

      void set_secret_key(uint8_t *, bool) override;

      void set_secret_key(const string &);
      void set_pin_code(const string &);

      // Trigger a full status read cycle (used by DanfossEcoManager for sequential polling)
      void trigger_update();
      // Initiate a BLE connection to flush pending HA commands without queuing new reads.
      // Used by DanfossEcoManager when a Home Assistant command arrived between poll cycles.
      void trigger_connect();
      // Force-terminate the current BLE session (used by DanfossEcoManager as a watchdog
      // when a session exceeds its time budget). Cleanup completes asynchronously once the
      // BLE stack reports the connection as closed; is_idle() flips to true at that point.
      void abort_session();
      // Returns true when the device has no BLE session in progress
      bool is_idle() const { return !this->active_; }

      // Called by DanfossEcoManager so that control() defers BLE connections to the manager
      void set_managed(bool managed) { this->managed_ = managed; }
      // Returns true when control() queued a write that has not been flushed yet
      bool has_pending_control() const { return this->control_pending_; }

    protected:
      void control(const ClimateCall &call) override;

      void connect();
      void disconnect();

      void write_pin();
      void on_write_pin(esp_ble_gattc_cb_param_t::gattc_write_evt_param);

      void on_read(esp_ble_gattc_cb_param_t::gattc_read_char_evt_param);
      void on_write(esp_ble_gattc_cb_param_t::gattc_write_evt_param);

      shared_ptr<Xxtea> xxtea;

      shared_ptr<WritableProperty> p_pin{nullptr};
      shared_ptr<BatteryProperty> p_battery{nullptr};
      shared_ptr<TemperatureProperty> p_temperature{nullptr};
      shared_ptr<SettingsProperty> p_settings{nullptr};
      shared_ptr<ErrorsProperty> p_errors{nullptr};
      shared_ptr<SecretKeyProperty> p_secret_key{nullptr};

      set<shared_ptr<DeviceProperty>> properties{};

    private:
      // Push a command, deleting it if the queue is full (prevents leaks)
      void enqueue_command_(Command *cmd);
      // Reset session bookkeeping after the BLE link is down. Drops stale READ
      // commands (re-queued on the next poll anyway) but keeps queued WRITEs so a
      // Home Assistant command survives a failed session.
      void finish_session_();

      ESPPreferenceObject secret_pref_;
      uint32_t pin_code_ = 0;

      uint8_t request_counter_ = 0;
      CommandQueue commands_;
      // True while a BLE session (connect, read/write exchange, disconnect) is in progress.
      // Set by trigger_update()/trigger_connect(), cleared by finish_session_().
      bool active_{false};
      // True when running under DanfossEcoManager; suppresses autonomous connect() in control()
      bool managed_{false};
      // True while a control()-originated write command is waiting to be sent
      bool control_pending_{false};
      // Number of failed attempts to flush a pending HA write; caps priority retries
      uint8_t control_attempts_{0};
    };

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
