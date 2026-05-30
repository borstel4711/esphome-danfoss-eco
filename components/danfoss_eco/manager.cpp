#include "manager.h"

#ifdef USE_ESP32

namespace esphome
{
  namespace danfoss_eco
  {

    static const char *const MANAGER_TAG = "danfoss_eco.manager";

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
      // ── Phase 1: wait for the active device to finish ────────────────────────
      if (this->active_device_ != nullptr)
      {
        if (!this->active_device_->is_idle())
          return; // still busy
        this->active_device_ = nullptr;
      }

      // ── Phase 2: handle pending HA commands (priority path) ──────────────────
      // Check whether any device received a Home Assistant command (e.g.
      // set_temperature) while we were busy with another device.  If so, start it
      // immediately regardless of the regular polling schedule.
      Device *priority = this->find_priority_device_();
      if (priority != nullptr)
      {
        ESP_LOGI(MANAGER_TAG, "DanfossEcoManager: handling priority HA command");
        this->active_device_ = priority;
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
      ESP_LOGD(MANAGER_TAG, "DanfossEcoManager: polling device %d/%d",
               (int)this->schedule_idx_ + 1, (int)this->devices_.size());
      this->schedule_idx_++;
      this->active_device_ = next;
      next->trigger_update();
    }

    void DanfossEcoManager::update()
    {
      if (this->devices_.empty())
        return;

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

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
