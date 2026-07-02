#include "manager.h"

#ifdef USE_ESP32

#include "esphome/core/hal.h"

namespace esphome
{
  namespace danfoss_eco
  {

    static const char *const MANAGER_TAG = "danfoss_eco.manager";

    // Pause between two device sessions, giving the BLE stack time to fully release
    // the previous connection before the next one is opened.
    static const uint32_t SETTLE_TIME_MS = 500;
    // Extra time granted after abort_session() for the BLE stack to tear the link down.
    // If the device is still not idle afterwards, the manager gives up on it and moves on.
    static const uint32_t ABORT_GRACE_MS = 15000;

    void DanfossEcoManager::setup()
    {
      // Stop the individual per-device polling timers and switch devices into
      // "managed" mode so that control() defers BLE connections to the manager.
      for (auto *device : this->devices_)
      {
        // stop_poller() is an ESPHome built-in (PollingComponent) that disables
        // the per-device update timer so the manager drives all polling instead.
        device->stop_poller();
        device->set_managed(true);
      }

      ESP_LOGD(MANAGER_TAG, "DanfossEcoManager: managing %d device(s), individual pollers stopped",
               (int)this->devices_.size());
    }

    void DanfossEcoManager::loop()
    {
      const uint32_t now = millis();

      // ── Phase 1: wait for the active device to finish (with watchdog) ────────
      if (this->active_device_ != nullptr)
      {
        if (this->active_device_->is_idle())
        {
          this->active_device_ = nullptr;
          this->next_start_at_ = now + SETTLE_TIME_MS;
        }
        else if (now - this->session_started_at_ > this->session_timeout_ + ABORT_GRACE_MS)
        {
          // The abort did not lead to an idle state in time - give up on this device
          // so the rest of the cycle can continue. The device cleans itself up as soon
          // as the BLE stack reports the link as closed.
          ESP_LOGE(MANAGER_TAG, "DanfossEcoManager: device did not become idle after abort, skipping it");
          this->active_device_ = nullptr;
          this->next_start_at_ = now + SETTLE_TIME_MS;
        }
        else if (now - this->session_started_at_ > this->session_timeout_)
        {
          if (!this->abort_sent_)
          {
            ESP_LOGW(MANAGER_TAG, "DanfossEcoManager: session exceeded %us, aborting",
                     (unsigned)(this->session_timeout_ / 1000));
            this->active_device_->abort_session();
            this->abort_sent_ = true;
          }
          return;
        }
        else
        {
          return; // still busy, within its time budget
        }
      }

      // ── Settle gate: give the BLE stack a moment between sessions ────────────
      if ((int32_t)(now - this->next_start_at_) < 0)
        return;

      // ── Phase 2: handle pending HA commands (priority path) ──────────────────
      // Check whether any device received a Home Assistant command (e.g.
      // set_temperature) while we were busy with another device.  If so, start it
      // immediately regardless of the regular polling schedule.
      Device *priority = this->find_priority_device_();
      if (priority != nullptr)
      {
        ESP_LOGI(MANAGER_TAG, "DanfossEcoManager: handling priority HA command");
        this->start_session_(priority);
        // trigger_connect() starts the BLE connection so the queued write is
        // flushed; on_write() will trigger a read cycle afterwards.
        priority->trigger_connect();
        return;
      }

      // ── Phase 3: advance the regular polling cycle ────────────────────────────
      if (!this->cycle_active_)
        return;

      if (this->schedule_idx_ >= this->devices_.size())
      {
        ESP_LOGI(MANAGER_TAG, "DanfossEcoManager: polling cycle complete (%d device(s))",
                 (int)this->devices_.size());
        this->cycle_active_ = false;
        return;
      }

      Device *next = this->devices_[this->schedule_idx_];
      this->schedule_idx_++;

      if (!next->is_idle())
      {
        // Should not happen under manager control; skip rather than pile on
        ESP_LOGW(MANAGER_TAG, "DanfossEcoManager: device %d/%d is busy, skipping this cycle",
                 (int)this->schedule_idx_, (int)this->devices_.size());
        return;
      }

      ESP_LOGD(MANAGER_TAG, "DanfossEcoManager: polling device %d/%d",
               (int)this->schedule_idx_, (int)this->devices_.size());
      this->start_session_(next);
      next->trigger_update();
    }

    void DanfossEcoManager::update()
    {
      if (this->devices_.empty())
        return;

      if (this->cycle_active_ || this->active_device_ != nullptr)
      {
        // Do not reset schedule_idx_ mid-cycle: that would starve the devices at the
        // end of the list. Increase update_interval if this warning shows up regularly.
        ESP_LOGW(MANAGER_TAG, "DanfossEcoManager: previous polling cycle still running, skipping this one");
        return;
      }

      ESP_LOGI(MANAGER_TAG, "DanfossEcoManager: starting polling cycle for %d device(s)",
               (int)this->devices_.size());

      // Reset sequence; loop() will start the first device on its next call.
      this->schedule_idx_ = 0;
      this->cycle_active_ = true;
    }

    void DanfossEcoManager::dump_config()
    {
      ESP_LOGCONFIG(MANAGER_TAG, "Danfoss Eco Manager:");
      ESP_LOGCONFIG(MANAGER_TAG, "  Devices: %d", (int)this->devices_.size());
      ESP_LOGCONFIG(MANAGER_TAG, "  Update Interval: %ums", this->get_update_interval());
      ESP_LOGCONFIG(MANAGER_TAG, "  Session Timeout: %ums", this->session_timeout_);
    }

    Device *DanfossEcoManager::find_priority_device_()
    {
      for (auto *d : this->devices_)
      {
        if (d->has_pending_control() && d->is_idle())
          return d;
      }
      return nullptr;
    }

    void DanfossEcoManager::start_session_(Device *device)
    {
      this->active_device_ = device;
      this->session_started_at_ = millis();
      this->abort_sent_ = false;
    }

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
