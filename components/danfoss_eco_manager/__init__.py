import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# danfoss_eco_manager component
#
# Coordinates sequential BLE polling of multiple Danfoss ECO eTRV devices.
# Devices are polled one after the other within a single cycle that is triggered
# at the configured update_interval. Only one BLE connection is active at a time,
# which avoids ESP32 BLE conflicts.
#
# Home Assistant control commands (set_temperature, set_mode) continue to work
# immediately because they use the Device's own connect()/command-queue path.
#
# Usage example:
#
#   danfoss_eco_manager:
#     update_interval: 15min
#     devices:
#       - id: trv_room
#       - id: trv_kitchen
#       - id: trv_bedroom
#       - id: trv_bathroom
#       - id: trv_office

CODEOWNERS = ["@dmitry-cherkas"]
DEPENDENCIES = ["danfoss_eco"]

CONF_DEVICES = "devices"

eco_ns = cg.esphome_ns.namespace("danfoss_eco")
DanfossEcoManager = eco_ns.class_("DanfossEcoManager", cg.PollingComponent)
DanfossDevice = eco_ns.class_("Device")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DanfossEcoManager),
        cv.Required(CONF_DEVICES): cv.ensure_list(cv.use_id(DanfossDevice)),
    }
).extend(cv.polling_component_schema("15min"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    for device_id in config[CONF_DEVICES]:
        device = await cg.get_variable(device_id)
        cg.add(var.add_device(device))
