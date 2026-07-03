#pragma once

#include "esphome/core/component.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#ifdef USE_ESP32

#include <set>

namespace esphome
{
    namespace danfoss_eco_scanner
    {
        using namespace std;
        using namespace esphome::esp32_ble_tracker;

        const char *const TAG = "danfoss_eco_scanner";

        class DanfossEcoScanner : public ESPBTDeviceListener, public Component
        {
        public:
            void dump_config() override;
            float get_setup_priority() const override { return setup_priority::DATA; }

            bool parse_device(const ESPBTDevice &device) override;

        private:
            // Addresses already reported. eTRVs advertise several times per second,
            // so without this each device would be logged over and over again.
            set<uint64_t> seen_{};
        };

    } // namespace danfoss_eco_scanner
} // namespace esphome

#endif
