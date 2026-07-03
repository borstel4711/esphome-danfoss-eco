#include "esphome/core/log.h"

#include "device_scanner.h"

#ifdef USE_ESP32

namespace esphome
{
    namespace danfoss_eco_scanner
    {
        static const string eTRV_SUFFIX = string(";eTRV");

        void DanfossEcoScanner::dump_config()
        {
            ESP_LOGCONFIG(TAG, "Danfoss Eco Scanner");
        }

        bool DanfossEcoScanner::parse_device(const ESPBTDevice &device)
        {
            const string &name = device.get_name();
            size_t s_len = eTRV_SUFFIX.length();

            if (name.length() <= s_len || name.compare(name.length() - s_len, s_len, eTRV_SUFFIX) != 0)
                return false;

            // Report each eTRV only once; advertisements repeat continuously.
            if (!this->seen_.insert(device.address_uint64()).second)
                return true;

            ESP_LOGI(TAG, "Found Danfoss eTRV - MAC: %s", device.address_str().c_str());
            ESP_LOGI(TAG, "  Add this MAC to your ble_client / danfoss_eco configuration.");
            return true;
        }

    } // namespace danfoss_eco_scanner
} // namespace esphome

#endif
