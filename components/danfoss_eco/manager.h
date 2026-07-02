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
    //   Every update_interval all registered devices are polled in order. Between two
    //   device sessions a short settle delay lets the BLE stack finish its teardown.
    //
    // Watchdog:
    //   A device session that exceeds session_timeout is aborted so a single
    //   unreachable eTRV cannot stall the whole polling cycle.
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
      void set_session_timeout(uint32_t timeout_ms) { this->session_timeout_ = timeout_ms; }

    protected:
      std::vector<Device *> devices_;

      // Index into devices_ for the *next* device to poll in the regular cycle
      size_t schedule_idx_{0};
      // Pointer to the device currently active (BLE connection in progress or data exchange)
      Device *active_device_{nullptr};
      // True while a regular polling cycle (triggered by update_interval) is running
      bool cycle_active_{false};

      // Maximum wall-clock time budget for a single device session (ms)
      uint32_t session_timeout_{60000};
      // millis() timestamp when the active device session was started
      uint32_t session_started_at_{0};
      // True once abort_session() was sent to the active device (avoid repeating it)
      bool abort_sent_{false};
      // millis() timestamp before which no new session may be started (settle delay)
      uint32_t next_start_at_{0};

    private:
      // Returns the first device that has a pending HA command and is idle (no active BLE)
      Device *find_priority_device_();
      // Marks a device session as started (bookkeeping for the watchdog)
      void start_session_(Device *device);
    };

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
