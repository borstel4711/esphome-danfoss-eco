import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, ble_client, sensor, binary_sensor, text_sensor
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_ICON,

    CONF_ENTITY_ID,
    CONF_UPDATE_INTERVAL,

    CONF_TEMPERATURE,
    CONF_BATTERY_LEVEL,

    CONF_VISUAL,
    CONF_MIN_TEMPERATURE,
    CONF_MAX_TEMPERATURE,
    
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_CELSIUS,
    
    CONF_DEVICE_CLASS,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_PROBLEM
)

CODEOWNERS = ["@dmitry-cherkas"]
DEPENDENCIES = ["ble_client"]
# load zero-configuration dependencies automatically
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "esp32_ble_tracker"]

CONF_PIN_CODE = 'pin_code'
CONF_SECRET_KEY = 'secret_key'
CONF_PROBLEMS = 'problems'
CONF_PROBLEMS_DETAIL = 'problems_detail'
CONF_PROBLEMS_DETAIL_DEFAULT_ICON = 'mdi:format-list-checks'
CONF_SUMMER_MODE = 'summer_mode'

eco_ns = cg.esphome_ns.namespace("danfoss_eco")
DanfossEco = eco_ns.class_(
    "Device", climate.Climate, ble_client.BLEClientNode, cg.PollingComponent
)
SummerModeController = eco_ns.class_("SummerModeController", cg.Component)

# Shared by the danfoss_eco climate platform and the danfoss_eco_manager component.
# While the referenced Home Assistant climate entity is "off" (heating disabled),
# polling is throttled to the summer update_interval to save eTRV battery.
SUMMER_MODE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SummerModeController),
            cv.Required(CONF_ENTITY_ID): cv.entity_id,
            cv.Optional(CONF_UPDATE_INTERVAL, default="2h"): cv.update_interval,
        }
    ),
    # HA state subscription runs over the native API - fail at validation time
    # with a clear message instead of at C++ compile time.
    cv.requires_component("api"),
)


async def summer_mode_to_code(config, target):
    ctrl = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(ctrl, {})
    cg.add(ctrl.set_target(target))
    cg.add(ctrl.set_entity_id(config[CONF_ENTITY_ID]))
    cg.add(ctrl.set_summer_interval(config[CONF_UPDATE_INTERVAL]))
    # HA state subscription support is compiled conditionally since ESPHome 2025.7;
    # the built-in homeassistant platforms set the same define.
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
    return ctrl

def validate_secret(value):
    value = cv.string_strict(value)
    if len(value) != 32:
        raise cv.Invalid("Secret key should be exactly 16 bytes (32 chars)")
    return value

def validate_pin(value):
    value = cv.string_strict(value)
    if len(value) != 4:
        raise cv.Invalid("PIN code should be exactly 4 chars")
    if not value.isnumeric():
        raise cv.Invalid("PIN code should be numeric")
    return value

CONFIG_SCHEMA = (
    climate.climate_schema(DanfossEco).extend(
        {
            cv.GenerateID(): cv.declare_id(DanfossEco),
            cv.Optional(CONF_SECRET_KEY): cv.sensitive(validate_secret),
            cv.Optional(CONF_PIN_CODE): cv.sensitive(validate_pin),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_VISUAL, default={
                CONF_MIN_TEMPERATURE:10,
                CONF_MAX_TEMPERATURE:30,
            }): cv.Schema(
                {
                    cv.Optional(CONF_MIN_TEMPERATURE): cv.temperature,
                    cv.Optional(CONF_MAX_TEMPERATURE): cv.temperature,
                }
            ),
            cv.Optional(CONF_PROBLEMS): binary_sensor.binary_sensor_schema().extend({
                cv.Optional(CONF_NAME): cv.string,
                cv.Optional(CONF_ENTITY_CATEGORY, default=ENTITY_CATEGORY_DIAGNOSTIC): cv.entity_category,
                cv.Optional(CONF_DEVICE_CLASS, default=DEVICE_CLASS_PROBLEM): binary_sensor.validate_device_class
            }),
            cv.Optional(CONF_PROBLEMS_DETAIL): text_sensor.text_sensor_schema().extend({
                cv.Optional(CONF_NAME): cv.string,
                cv.Optional(CONF_ENTITY_CATEGORY, default=ENTITY_CATEGORY_DIAGNOSTIC): cv.entity_category,
                cv.Optional(CONF_ICON, default=CONF_PROBLEMS_DETAIL_DEFAULT_ICON): cv.icon,
            }),
            # Ignored (with a runtime warning) when the device is managed by
            # danfoss_eco_manager - configure summer_mode on the manager instead.
            cv.Optional(CONF_SUMMER_MODE): SUMMER_MODE_SCHEMA
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    # 15min keeps the eTRV state reasonably fresh while limiting BLE sessions to
    # ~96/day per device - each session costs eTRV battery. HA commands are sent
    # immediately regardless of this interval.
    .extend(cv.polling_component_schema("15min"))
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await ble_client.register_ble_node(var, config)
    
    cg.add(var.set_secret_key(config.get(CONF_SECRET_KEY, "")))
    cg.add(var.set_pin_code(config.get(CONF_PIN_CODE, "")))
    
    if CONF_BATTERY_LEVEL in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level(sens))
    if CONF_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_TEMPERATURE])
        cg.add(var.set_temperature(sens))
    if CONF_PROBLEMS in config:
        b_sens = await binary_sensor.new_binary_sensor(config[CONF_PROBLEMS])
        cg.add(var.set_problems(b_sens))
    if CONF_PROBLEMS_DETAIL in config:
        t_sens = await text_sensor.new_text_sensor(config[CONF_PROBLEMS_DETAIL])
        cg.add(var.set_problems_detail(t_sens))
    if CONF_SUMMER_MODE in config:
        ctrl = await summer_mode_to_code(config[CONF_SUMMER_MODE], var)
        # enables the runtime guard against throttling a managed device
        cg.add(ctrl.set_device(var))

