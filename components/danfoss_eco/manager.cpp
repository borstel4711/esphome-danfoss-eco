#include "manager.h"

#ifdef USE_ESP32

namespace esphome
{
  namespace danfoss_eco
  {

    void DanfossEcoManager::setup()
    {
      // Stop the individual per-device polling timers so that the manager drives
      // all polling sequentially. Home-Assistant control commands are not affected
      // because they use a separate connect()/command-queue path in Device.
      for (auto *device : this->devices_)
        device->stop_poller();

      ESP_LOGD(TAG, "DanfossEcoManager: managing %d device(s), individual pollers stopped",
               (int)this->devices_.size());
    }

    void DanfossEcoManager::loop()
    {
      if (!this->cycle_active_)
        return;

      if (this->current_idx_ >= this->devices_.size())
      {
        // All devices in this cycle have been handled
        ESP_LOGI(TAG, "DanfossEcoManager: polling cycle complete (%d device(s))",
                 (int)this->devices_.size());
        this->cycle_active_ = false;
        return;
      }

      Device *current = this->devices_[this->current_idx_];
      if (!current->is_idle())
        return; // still working, wait

      // Current device finished – advance to the next one
      this->current_idx_++;
      if (this->current_idx_ < this->devices_.size())
      {
        ESP_LOGD(TAG, "DanfossEcoManager: starting device %d/%d",
                 (int)this->current_idx_ + 1, (int)this->devices_.size());
        this->devices_[this->current_idx_]->trigger_update();
      }
    }

    void DanfossEcoManager::update()
    {
      if (this->devices_.empty())
        return;

      ESP_LOGI(TAG, "DanfossEcoManager: starting polling cycle for %d device(s)",
               (int)this->devices_.size());

      this->current_idx_ = 0;
      this->cycle_active_ = true;
      this->devices_[0]->trigger_update();
    }

    void DanfossEcoManager::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Danfoss Eco Manager:");
      ESP_LOGCONFIG(TAG, "  Devices: %d", (int)this->devices_.size());
      ESP_LOGCONFIG(TAG, "  Update Interval: %ums", this->get_update_interval());
    }

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
