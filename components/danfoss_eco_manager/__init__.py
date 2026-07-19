import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from esphome.components.danfoss_eco.climate import (
    CONF_SUMMER_MODE,
    SUMMER_MODE_SCHEMA,
    summer_mode_to_code,
)

# danfoss_eco_manager component
#
# Coordinates sequential BLE polling of multiple Danfoss ECO eTRV devices.
# Devices are polled one after the other within a single cycle that is triggered
# at the configured update_interval. Only one BLE connection is active at a time,
# which avoids ESP32 BLE conflicts.
#
# A per-device session watchdog (session_timeout) makes sure a single unreachable
# eTRV cannot stall the polling cycle: its session is aborted and the manager
# continues with the next device.
#
# Home Assistant control commands (set_temperature, set_mode) are queued by the
# device and flushed by the manager with priority, as soon as the currently
# active BLE session (if any) has finished.
#
# Usage example:
#
#   danfoss_eco_manager:
#     update_interval: 15min
#     session_timeout: 60s
#     summer_mode:                  # optional: throttle polling while heating is off
#       entity_id: climate.heating
#       update_interval: 2h
#     devices:
#       - id: trv_room
#       - id: trv_kitchen
#       - id: trv_bedroom
#       - id: trv_bathroom
#       - id: trv_office

CODEOWNERS = ["@dmitry-cherkas"]
# NOTE: no DEPENDENCIES on "danfoss_eco" here - it is a climate *platform*, not a
# top-level component, so a dependency on it can never be satisfied. The devices
# list below (cv.use_id) already enforces that danfoss_eco climates exist.

CONF_DEVICES = "devices"
CONF_SESSION_TIMEOUT = "session_timeout"

eco_ns = cg.esphome_ns.namespace("danfoss_eco")
DanfossEcoManager = eco_ns.class_("DanfossEcoManager", cg.PollingComponent)
DanfossDevice = eco_ns.class_("Device")

# accepts both "- trv_room" and "- id: trv_room" list entries
DEVICE_SCHEMA = cv.maybe_simple_value(
    cv.Schema({cv.Required(CONF_ID): cv.use_id(DanfossDevice)}),
    key=CONF_ID,
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DanfossEcoManager),
        cv.Required(CONF_DEVICES): cv.ensure_list(DEVICE_SCHEMA),
        cv.Optional(
            CONF_SESSION_TIMEOUT, default="60s"
        ): cv.positive_time_period_milliseconds,
        # While the referenced HA climate entity is "off" (heating disabled), the
        # polling cycle is throttled to the summer update_interval (default 2h)
        # to save eTRV battery. HA commands are still flushed immediately.
        cv.Optional(CONF_SUMMER_MODE): SUMMER_MODE_SCHEMA,
    }
).extend(cv.polling_component_schema("15min"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_session_timeout(config[CONF_SESSION_TIMEOUT]))

    for device_conf in config[CONF_DEVICES]:
        device = await cg.get_variable(device_conf[CONF_ID])
        cg.add(var.add_device(device))

    if CONF_SUMMER_MODE in config:
        # target is the manager's own poller - it drives all device polling
        await summer_mode_to_code(config[CONF_SUMMER_MODE], var)
