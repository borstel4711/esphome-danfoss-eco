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
    // Each device is polled one after the other so only one BLE connection is active
    // at a time, which avoids ESP32 BLE conflicts.
    //
    // Normal operation:
    //   Every update_interval all registered devices are polled in order.
    //
    // Home Assistant command handling (priority path):
    //   When HA sends a command (e.g. set_temperature), Device::control() queues the
    //   write and sets a control_pending flag instead of opening a BLE connection
    //   immediately. The manager detects this flag in loop(), waits for the current
    //   device to finish, and then connects to the priority device to flush the write.
    //   After the write completes Device::on_write() triggers a full read cycle so HA
    //   sees fresh state. Only then does the manager continue with the normal schedule.
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

      // Index into devices_ for the *next* device to poll in the regular cycle
      size_t schedule_idx_{0};
      // Pointer to the device currently active (BLE connection in progress or data exchange)
      Device *active_device_{nullptr};
      // True while a regular polling cycle (triggered by update_interval) is running
      bool cycle_active_{false};

    private:
      // Returns the first device that has a pending HA command and is idle (no active BLE)
      Device *find_priority_device_();
    };

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
