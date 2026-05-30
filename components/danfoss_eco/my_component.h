#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/climate/climate.h"

#include "helpers.h"

namespace esphome
{
    namespace danfoss_eco
    {
        using namespace std;
        using namespace esphome::climate;
        using namespace esphome::sensor;
        using namespace esphome::binary_sensor;
        using namespace esphome::text_sensor;

        class MyComponent : public Climate, public PollingComponent
        {
        public:
            float get_setup_priority() const override { return setup_priority::DATA; }

            ClimateTraits traits() override
            {
                auto traits = ClimateTraits();

                // supports reporting current temperature
                traits.add_feature_flags(CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
                // supports reporting current action
                traits.add_feature_flags(CLIMATE_SUPPORTS_ACTION);

                traits.add_supported_mode(ClimateMode::CLIMATE_MODE_HEAT);
                traits.add_supported_mode(ClimateMode::CLIMATE_MODE_AUTO);
                traits.set_visual_temperature_step(0.5);
                return traits;
            }

            void set_battery_level(Sensor *battery_level) { battery_level_ = battery_level; }
            void set_temperature(Sensor *temperature) { temperature_ = temperature; }
            void set_problems(BinarySensor *problems) { problems_ = problems; }
            void set_problems_detail(TextSensor *problems_detail) { problems_detail_ = problems_detail; }

            Sensor *battery_level() { return this->battery_level_; }
            Sensor *temperature() { return this->temperature_; }
            BinarySensor *problems() { return this->problems_; }
            TextSensor *problems_detail() { return this->problems_detail_; }

            virtual void set_secret_key(uint8_t *, bool) = 0;

        protected:
            Sensor *battery_level_{nullptr};
            Sensor *temperature_{nullptr};
            BinarySensor *problems_{nullptr};
            TextSensor *problems_detail_{nullptr};
        };

    } // namespace danfoss_eco
} // namespace esphome