#pragma once

#include "esphome/core/component.h"

#ifdef USE_ESP32

#include <string>

namespace esphome
{
  namespace danfoss_eco
  {

    class Device;

    // SummerModeController throttles a PollingComponent (a standalone Device or the
    // DanfossEcoManager) based on the state of a Home Assistant climate entity.
    //
    // While that entity reports "off" (heating disabled, e.g. during summer) the
    // polling interval is switched to summer_interval_, so BLE sessions - each of
    // which costs eTRV battery - become rare. Any other state ("heat", "auto",
    // "unavailable", ...) restores the configured interval; leaving summer mode
    // additionally triggers an immediate poll so Home Assistant gets fresh data.
    //
    // Home Assistant commands (set_temperature etc.) bypass the polling schedule
    // entirely and are never delayed by summer mode.
    class SummerModeController : public Component
    {
    public:
      // AFTER_CONNECTION mirrors ESPHome's own homeassistant platform: by the time
      // setup() runs the API server exists, so the subscription can be registered
      // unconditionally. HA pushes the current entity state on every (re)connect.
      float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

      void setup() override;
      void dump_config() override;

      void set_entity_id(const std::string &entity_id) { this->entity_id_ = entity_id; }
      void set_summer_interval(uint32_t interval_ms) { this->summer_interval_ = interval_ms; }
      // The poller to throttle: the Device itself (standalone) or the DanfossEcoManager
      void set_target(PollingComponent *target) { this->target_ = target; }
      // Standalone only: enables the guard against throttling a managed device,
      // whose own poller is intentionally stopped by the manager.
      void set_device(Device *device) { this->device_ = device; }

    protected:
      void on_ha_state_(const std::string &state);
      void apply_interval_(uint32_t interval_ms);

      std::string entity_id_;
      uint32_t summer_interval_{2 * 60 * 60 * 1000};
      // The normal interval configured in YAML, captured in setup()
      uint32_t normal_interval_{0};
      PollingComponent *target_{nullptr};
      Device *device_{nullptr};
      bool summer_active_{false};
      // Set when summer_mode was configured on a managed device (misconfiguration)
      bool disabled_{false};
    };

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
