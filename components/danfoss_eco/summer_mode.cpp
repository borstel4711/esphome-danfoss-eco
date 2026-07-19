#include "summer_mode.h"

#ifdef USE_ESP32

#include "device.h"
#include "esphome/core/log.h"

#include <functional>
#include <utility>

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

namespace esphome
{
  namespace danfoss_eco
  {

    static const char *const SUMMER_TAG = "danfoss_eco.summer_mode";

    void SummerModeController::setup()
    {
      if (this->device_ != nullptr && this->device_->is_managed())
      {
        // The manager stopped this device's poller and drives all polling itself.
        // This guard is reliable because DanfossEcoManager::setup() (DATA priority)
        // runs before this setup() (AFTER_CONNECTION priority).
        ESP_LOGW(SUMMER_TAG, "summer_mode on a managed device is ignored - configure it on danfoss_eco_manager instead");
        this->disabled_ = true;
        return;
      }

      this->normal_interval_ = this->target_->get_update_interval();

#if defined(USE_API) && defined(USE_API_HOMEASSISTANT_STATES)
      if (api::global_api_server != nullptr)
      {
        // Explicit std::function type: a bare lambda would be ambiguous between the
        // StringRef and const std::string& overloads of subscribe_home_assistant_state
        std::function<void(const std::string &)> cb = [this](const std::string &state)
        { this->on_ha_state_(state); };
        api::global_api_server->subscribe_home_assistant_state(this->entity_id_, {}, std::move(cb));
      }
#endif
    }

    void SummerModeController::on_ha_state_(const std::string &state)
    {
      // Only the exact climate state "off" activates summer mode. "heat", "auto",
      // "unavailable", "unknown" etc. all keep/restore the normal interval, so a
      // vanished entity can never leave the poller stuck at the slow rate.
      bool summer = (state == "off");
      if (summer == this->summer_active_)
        return; // HA re-pushes the current state on every reconnect - ignore no-ops

      this->summer_active_ = summer;
      if (summer)
      {
        ESP_LOGI(SUMMER_TAG, "'%s' is off - summer mode ON, polling every %u min",
                 this->entity_id_.c_str(), (unsigned)(this->summer_interval_ / 60000));
        this->apply_interval_(this->summer_interval_);
      }
      else
      {
        ESP_LOGI(SUMMER_TAG, "'%s' is '%s' - summer mode OFF, restoring %u min interval and polling now",
                 this->entity_id_.c_str(), state.c_str(), (unsigned)(this->normal_interval_ / 60000));
        this->apply_interval_(this->normal_interval_);
        // Catch-up poll so Home Assistant gets fresh state right away instead of
        // waiting up to one normal interval.
        this->target_->update();
      }
    }

    void SummerModeController::apply_interval_(uint32_t interval_ms)
    {
      // set_update_interval() alone does not reschedule a running poller; the
      // stop/start pair applies the new interval and also cancels an already
      // scheduled poll when entering summer mode.
      this->target_->stop_poller();
      this->target_->set_update_interval(interval_ms);
      this->target_->start_poller();
    }

    void SummerModeController::dump_config()
    {
      ESP_LOGCONFIG(SUMMER_TAG, "Summer Mode:");
      ESP_LOGCONFIG(SUMMER_TAG, "  Entity ID: '%s'", this->entity_id_.c_str());
      ESP_LOGCONFIG(SUMMER_TAG, "  Summer Update Interval: %u min", (unsigned)(this->summer_interval_ / 60000));
      if (this->disabled_)
        ESP_LOGCONFIG(SUMMER_TAG, "  Status: DISABLED (device is managed by danfoss_eco_manager)");
      else
        ESP_LOGCONFIG(SUMMER_TAG, "  Currently active: %s", this->summer_active_ ? "YES" : "NO");
    }

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
