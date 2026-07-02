#include "device.h"

#ifdef USE_ESP32

#include <vector>

namespace esphome
{
  namespace danfoss_eco
  {
    // Maximum attempts to flush a pending HA write before dropping it. Prevents an
    // unreachable device from being retried (and blocking the manager) forever.
    static const uint8_t MAX_CONTROL_ATTEMPTS = 3;
    // Consecutive PIN rejections after which the device is considered misconfigured
    // (wrong pin_code) and disabled - a wrong PIN is not transient, and retrying it
    // every cycle wastes a full BLE session (and eTRV battery) each time.
    static const uint8_t MAX_PIN_FAILURES = 5;

    void Device::setup()
    {
      shared_ptr<MyComponent> sp_this(this, [](MyComponent *) {});

      this->p_pin = make_shared<WritableProperty>(sp_this, xxtea, SERVICE_SETTINGS, CHARACTERISTIC_PIN);
      this->p_battery = make_shared<BatteryProperty>(sp_this, xxtea);
      this->p_temperature = make_shared<TemperatureProperty>(sp_this, xxtea);
      this->p_settings = make_shared<SettingsProperty>(sp_this, xxtea);
      this->p_errors = make_shared<ErrorsProperty>(sp_this, xxtea);
      this->p_secret_key = make_shared<SecretKeyProperty>(sp_this, xxtea);

      this->properties = {this->p_pin, this->p_battery, this->p_temperature, this->p_settings, this->p_errors, this->p_secret_key};

      // This component polls devices by MAC address on its own schedule. Advertisement-triggered
      // auto-connects would open BLE connections outside that schedule (and, with several eTRVs,
      // several at once) - the main source of instability with more than 2 devices.
      this->parent()->set_auto_connect(false);
      // eTRVs use a public MAC address; populate the client with it so no scan is needed.
      copy_address(this->parent()->get_address(), this->parent()->get_remote_bda());
      // Initializes node_state of all client nodes (BLEClientNode leaves it uninitialized
      // until the first BLEClient::set_state() call).
      this->parent()->set_state(ClientState::INIT);
    }

    void Device::loop()
    {
      if (this->status_has_error())
      {
        this->disconnect();
        this->status_clear_error();
      }

      // node_state is kept in sync with the BLE client by BLEClient::set_state(), so this
      // detects both a graceful session end and an unexpected disconnect (link loss, remote
      // close, failed connection attempt). Without this, a dropped link would leave the
      // device "busy" forever and stall the manager's polling cycle.
      if (this->active_ && this->node_state == ClientState::IDLE)
      {
        this->finish_session_();
        return;
      }

      if (this->node_state != ClientState::ESTABLISHED)
        return;

      // Serialize GATT operations: issue the next command only when nothing is in flight.
      // Firing multiple parallel read requests congests the Bluedroid stack and produces
      // sporadic ESP_GATT_ERROR/timeouts, especially with several devices per cycle.
      if (this->request_counter_ > 0)
        return;

      Command *cmd = this->commands_.pop();
      if (cmd != nullptr)
      {
        if (cmd->execute(this->parent()))
          this->request_counter_++;

        delete cmd;
        return;
      }

      // no queued commands and no pending requests - we are done with the device for now
      this->disconnect();
    }

    void Device::enqueue_command_(Command *cmd)
    {
      if (!this->commands_.push(cmd))
      {
        ESP_LOGW(TAG, "[%s] command queue full, dropping command", this->get_name().c_str());
        delete cmd;
      }
    }

    void Device::finish_session_()
    {
      this->active_ = false;
      this->request_counter_ = 0;
      if (this->parent()->enabled)
        this->parent()->set_enabled(false);

      // Drop stale READ commands - they would otherwise pile up across failed sessions.
      // Keep WRITE commands (Home Assistant changes) and mark them pending again so the
      // manager retries them, but only a bounded number of times.
      std::vector<Command *> writes;
      Command *cmd;
      while ((cmd = this->commands_.pop()) != nullptr)
      {
        if (cmd->type == CommandType::WRITE)
          writes.push_back(cmd);
        else
          delete cmd;
      }

      if (!writes.empty())
      {
        if (++this->control_attempts_ >= MAX_CONTROL_ATTEMPTS)
        {
          ESP_LOGE(TAG, "[%s] dropping %d unsent command(s) after %d failed attempts",
                   this->get_name().c_str(), (int)writes.size(), (int)this->control_attempts_);
          for (auto *w : writes)
            delete w;
          this->control_pending_ = false;
          this->control_attempts_ = 0;
        }
        else
        {
          for (auto *w : writes)
            this->enqueue_command_(w);
          this->control_pending_ = true;
          ESP_LOGW(TAG, "[%s] session ended with %d unsent command(s), will retry",
                   this->get_name().c_str(), (int)writes.size());
        }
      }
    }

    void Device::trigger_update()
    {
      if (this->is_failed())
        return;

      // Clear the priority flag before connecting. Any queued WRITE commands
      // (from a prior control() call) remain in commands_ and will be executed
      // before the READ commands queued below - the flag itself is only the
      // manager's hint for prioritising this device in find_priority_device_().
      this->control_pending_ = false;

      if (!this->active_)
      {
        if (this->node_state != ClientState::IDLE)
        {
          // BLE stack not up yet (INIT) or previous teardown still running
          // (DISCONNECTING) - skip this poll, the next cycle retries.
          ESP_LOGW(TAG, "[%s] BLE client not ready (state %d), poll skipped", this->get_name().c_str(), (int)this->node_state);
          return;
        }
        this->active_ = true;
        this->connect();
      }
      else if (this->node_state != ClientState::ESTABLISHED)
      {
        // A session is already starting up - queuing more reads now would only
        // accumulate duplicates.
        return;
      }

      if (this->xxtea->status() == XXTEA_STATUS_SUCCESS)
      {
        ESP_LOGI(TAG, "[%s] requesting device state", this->get_name().c_str());

        this->enqueue_command_(new Command(CommandType::READ, this->p_battery));
        this->enqueue_command_(new Command(CommandType::READ, this->p_temperature));
        this->enqueue_command_(new Command(CommandType::READ, this->p_settings));
        this->enqueue_command_(new Command(CommandType::READ, this->p_errors));
      }
    }

    void Device::trigger_connect()
    {
      // Flush pending write commands over a fresh BLE session (called by
      // DanfossEcoManager for the priority path and by control() in standalone
      // mode). on_write() triggers a full read cycle once the write completes.
      this->control_pending_ = false;

      if (this->is_failed() || this->active_)
        return;

      if (this->node_state != ClientState::IDLE)
        return; // client not settled; the queued write is flushed with the next session

      this->active_ = true;
      this->connect();
    }

    void Device::abort_session()
    {
      ESP_LOGW(TAG, "[%s] aborting BLE session", this->get_name().c_str());

      // A client stuck in DISCOVERED was never promoted by the tracker - there is no
      // connection to tear down, and BLEClientBase::disconnect() would only set a flag
      // and wait for a connection that may never come. Revert it to IDLE directly;
      // loop() then finishes the session bookkeeping.
      if (this->node_state == ClientState::DISCOVERED)
        this->parent()->set_state(ClientState::IDLE);

      this->disconnect();
    }

    void Device::update()
    {
      this->trigger_update();
    }

    void Device::control(const ClimateCall &call)
    {
      if (call.get_target_temperature().has_value())
      {
        TemperatureData &t_data = (TemperatureData &)(*this->p_temperature->data);
        t_data.target_temperature = *call.get_target_temperature();

        this->enqueue_command_(new Command(CommandType::WRITE, this->p_temperature));
        this->control_pending_ = true;
        this->control_attempts_ = 0;
        // In managed mode the manager picks up control_pending_ and schedules the
        // connection; in standalone mode we start the session ourselves. Going through
        // trigger_connect() (not bare connect()) marks the session active_, so the
        // failed-write retry path in finish_session_() also covers standalone devices.
        if (!this->managed_)
          this->trigger_connect();
      }

      if (call.get_mode().has_value())
      {
        SettingsData &s_data = (SettingsData &)(*this->p_settings->data);
        s_data.device_mode = *call.get_mode();

        // update state immediately to avoid delays in HA UI
        this->mode = s_data.device_mode;
        this->publish_state();

        this->enqueue_command_(new Command(CommandType::WRITE, this->p_settings));
        this->control_pending_ = true;
        this->control_attempts_ = 0;
        if (!this->managed_)
          this->trigger_connect();
      }
    }

    void Device::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
    {
      switch (event)
      {
      case ESP_GATTC_CONNECT_EVT:
        if (memcmp(param->connect.remote_bda, this->parent()->get_remote_bda(), 6) != 0)
          return; // event does not belong to this client, exit gattc_event_handler

        ESP_LOGD(TAG, "[%s] connect, conn_id=%d", this->get_name().c_str(), param->connect.conn_id);
        break;

      case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK)
          ESP_LOGV(TAG, "[%s] open, conn_id=%d", this->get_name().c_str(), param->open.conn_id);
        else
          ESP_LOGW(TAG, "[%s] failed to open, conn_id=%d, status=%#04x", this->get_name().c_str(), param->open.conn_id, param->open.status);
        break;

      case ESP_GATTC_CLOSE_EVT:
        if (param->close.status == ESP_GATT_OK)
          ESP_LOGV(TAG, "[%s] close, conn_id=%d, reason=%d", this->get_name().c_str(), param->close.conn_id, param->close.reason);
        else
          ESP_LOGW(TAG, "[%s] failed to close, conn_id=%d, status=%#04x", this->get_name().c_str(), param->close.conn_id, param->close.status);
        break;

      case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGD(TAG, "[%s] disconnect, conn_id=%d, reason=%#04x", this->get_name().c_str(), param->disconnect.conn_id, (int)param->disconnect.reason);
        break;

      case ESP_GATTC_SEARCH_CMPL_EVT:
        for (auto p : this->properties)
          p->init_handle(this->parent());

        write_pin();
        break;

      case ESP_GATTC_WRITE_CHAR_EVT:
        if (param->write.handle == this->p_pin->handle)
          this->on_write_pin(param->write);
        else
          this->on_write(param->write);
        break;

      case ESP_GATTC_READ_CHAR_EVT:
        this->on_read(param->read);
        break;

      default:
        ESP_LOGV(TAG, "[%s] unhandled event: event=%d, gattc_if=%d", this->get_name().c_str(), (int)event, gattc_if);
        break;
      }
    }

    void Device::write_pin()
    {
      ESP_LOGD(TAG, "[%s] writing pin", this->get_name().c_str());

      uint8_t pin_bytes[sizeof(uint32_t)];
      write_int(pin_bytes, 0, this->pin_code_);

      if (!this->p_pin->write_request(this->parent(), pin_bytes, sizeof(pin_bytes)))
        this->status_set_error();
    }

    void Device::on_read(esp_ble_gattc_cb_param_t::gattc_read_char_evt_param param)
    {
      if (this->request_counter_ > 0)
        this->request_counter_--;
      if (param.status != ESP_GATT_OK)
      {
        ESP_LOGW(TAG, "[%s] failed to read characteristic: handle=%#04x, status=%#04x", this->get_name().c_str(), param.handle, param.status);
        return;
      }

      auto device_property = find_if(properties.begin(), properties.end(),
                                     [&param](shared_ptr<DeviceProperty> p)
                                     { return p->handle == param.handle; });

      if (device_property != properties.end())
        (*device_property)->update_state(param.value, param.value_len);
      else
        ESP_LOGW(TAG, "[%s] unknown property with handle=%#04x", this->get_name().c_str(), param.handle);
    }

    void Device::on_write(esp_ble_gattc_cb_param_t::gattc_write_evt_param param)
    {
      if (this->request_counter_ > 0)
        this->request_counter_--;
      if (param.status != ESP_GATT_OK)
      {
        ESP_LOGW(TAG, "[%s] failed to write characteristic: handle=%#04x, status=%#04x", this->get_name().c_str(), param.handle, param.status);
      }
      else
      {
        // write flushed successfully - reset the retry budget and refresh full state so
        // Home Assistant reflects what the device actually accepted
        this->control_attempts_ = 0;
        update();
      }
    }

    void Device::on_write_pin(esp_ble_gattc_cb_param_t::gattc_write_evt_param param)
    {
      if (param.status != ESP_GATT_OK)
      {
        // A single failed PIN write can be a transient BLE error, so don't disable the
        // device right away (the old mark_failed() bricked it until reboot). But a wrong
        // pin_code fails deterministically - after several consecutive rejections stop
        // retrying, otherwise every cycle wastes a full BLE session and eTRV battery.
        this->pin_failures_++;
        ESP_LOGE(TAG, "[%s] pin FAILED, status=%#04x (attempt %d/%d)", this->get_name().c_str(), param.status,
                 (int)this->pin_failures_, (int)MAX_PIN_FAILURES);
        if (this->pin_failures_ >= MAX_PIN_FAILURES)
        {
          ESP_LOGE(TAG, "[%s] PIN rejected %d times in a row - check pin_code, disabling device", this->get_name().c_str(), (int)this->pin_failures_);
          this->mark_failed();
        }
        this->disconnect();
        return;
      }

      ESP_LOGD(TAG, "[%s] pin OK", this->get_name().c_str());
      this->pin_failures_ = 0;
      this->node_state = ClientState::ESTABLISHED;

      // after PIN is written, we might need to read the secret_key from the device
      if (this->xxtea->status() == XXTEA_STATUS_NOT_INITIALIZED && this->p_secret_key->handle != INVALID_HANDLE)
      {
        ESP_LOGD(TAG, "[%s] attempting to read the device secret_key", this->get_name().c_str());
        this->enqueue_command_(new Command(CommandType::READ, this->p_secret_key));
      }
    }

    void Device::connect()
    {
      // Only start a connection from a settled state. INIT means the BLE stack is not up
      // yet; DISCONNECTING means the previous teardown has not completed - in both cases
      // the poll is skipped and the next cycle retries.
      if (this->node_state != ClientState::IDLE)
      {
        if (this->node_state == ClientState::INIT || this->node_state == ClientState::DISCONNECTING)
          ESP_LOGW(TAG, "[%s] cannot connect right now (client state: %d), poll skipped", this->get_name().c_str(), (int)this->node_state);
        return;
      }

      if (this->xxtea->status() == XXTEA_STATUS_NOT_INITIALIZED)
        ESP_LOGI(TAG, "[%s] Short press Danfoss Eco hardware button NOW in order to allow reading the secret key", this->get_name().c_str());

      if (!parent()->enabled)
      {
        ESP_LOGD(TAG, "[%s] re-enabling ble_client", this->get_name().c_str());
        parent()->set_enabled(true);
      }

      // Hand the connection request to esp32_ble_tracker by marking the client as
      // DISCOVERED: the tracker stops any running scan itself and serializes connection
      // attempts (one client at a time). Calling esp_ble_gattc_open() directly while a
      // scan is active is a well-known source of ESP_GATT_ERROR (0x85) failures.
      this->parent()->set_state(ClientState::DISCOVERED);
    }

    void Device::disconnect()
    {
      // Triggers BLEClientBase::disconnect(); once the stack reports the link as closed,
      // the client state becomes IDLE and loop() finishes the session bookkeeping.
      this->parent()->set_enabled(false);
    }

    void Device::set_pin_code(const string &str)
    {
      if (str.length() > 0)
        this->pin_code_ = atoi((const char *)str.c_str());

      ESP_LOGD(TAG, "[%s] PIN: %04d", this->get_name().c_str(), this->pin_code_);
    }

    void Device::set_secret_key(const string &str)
    {
      // initialize the preference object
      uint32_t hash = fnv1_hash("danfoss_eco_secret__" + this->get_name());
      this->secret_pref_ = global_preferences->make_preference<SecretKeyValue>(hash, true);

      if (str.length() > 0)
      {
        uint8_t buff[SECRET_KEY_LENGTH];
        ESP_LOGD(TAG, "[%s] secret_key was passed via config", this->get_name().c_str());
        parse_hex_str(str.c_str(), 32, buff);
        this->set_secret_key(buff, false);
      }
      else
      {
        auto key_buff = SecretKeyValue();
        if (this->secret_pref_.load(&key_buff))
        {
          // use persisted secret value
          ESP_LOGD(TAG, "[%s] secret_key was loaded from flash", this->get_name().c_str());
          this->set_secret_key(key_buff.value, false);
        }
      }
    }

    void Device::set_secret_key(uint8_t *key, bool persist)
    {
      ESP_LOGD(TAG, "[%s] secret_key bytes: %s", this->get_name().c_str(), format_hex_pretty(key, SECRET_KEY_LENGTH).c_str());

      int status = this->xxtea->set_key(key, SECRET_KEY_LENGTH);
      if (status != XXTEA_STATUS_SUCCESS)
      {
        ESP_LOGE(TAG, "xxtea initialization failed, status: %d", status);
        this->mark_failed();
      }
      else if (persist)
      {
        // if xxtea was initialized successfully and secret_key should be persisted
        auto key_buff = SecretKeyValue(key);
        this->secret_pref_.save(&key_buff);
        global_preferences->sync();

        ESP_LOGI(TAG, "[%s] secret_key was saved to flash", this->get_name().c_str());
      }
    }

  } // namespace danfoss_eco
} // namespace esphome

#endif
