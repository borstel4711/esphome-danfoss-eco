#pragma once

#include "esphome/core/component.h"
#include "device.h"

#ifdef USE_ESP32

#include <vector>

namespace esphome
{
  namespace danfoss_eco
  {

    // DanfossEcoManager polls up to 5 eTRV devices sequentially in a single cycle.
    // Each device is polled one after the other, so only one BLE connection is active
    // at a time. The manager fires its own polling cycle at the configured update_interval
    // and disables the individual per-device polling timers during setup.
    //
    // Home Assistant control commands (temperature changes, mode changes) are handled
    // immediately by the Device's existing control() path and are unaffected by the
    // manager's scheduling.
    class DanfossEcoManager : public PollingComponent
    {
    public:
      float get_setup_priority() const override { return setup_priority::DATA; }

      void setup() override;
      void loop() override;
      void update() override;
      void dump_config() override;

      void add_device(Device *device) { this->devices_.push_back(device); }

    protected:
      std::vector<Device *> devices_;
      size_t current_idx_{0};
      bool cycle_active_{false};
    };

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
