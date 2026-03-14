import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import sensor, web_server_base, update

# Make sure web_server_base and update core are loaded
AUTO_LOAD = ["web_server_base", "update"]

aether_ns = cg.esphome_ns.namespace("aether")
AetherUI = aether_ns.class_("AetherUI", cg.Component)

CONF_CO2 = "co2"
CONF_PM25 = "pm25"
CONF_TEMP = "temp"
CONF_RH = "rh"
CONF_PM1 = "pm1"
CONF_PM4 = "pm4"
CONF_PM10 = "pm10"
CONF_VOC = "voc"
CONF_NOX = "nox"
CONF_FW_UPDATE = "fw_update"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(AetherUI),

        cv.Required(CONF_CO2): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PM25): cv.use_id(sensor.Sensor),
        cv.Required(CONF_TEMP): cv.use_id(sensor.Sensor),
        cv.Required(CONF_RH): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PM1): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PM4): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PM10): cv.use_id(sensor.Sensor),
        cv.Required(CONF_VOC): cv.use_id(sensor.Sensor),
        cv.Required(CONF_NOX): cv.use_id(sensor.Sensor),

        # Optional link to the update entity created by:
        # update:
        #   - platform: http_request
        #     id: aether_fw_update
        cv.Optional(CONF_FW_UPDATE): cv.use_id(update.UpdateEntity),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Wire up all sensor pointers → set_co2, set_pm25, etc. in C++
    for key in [
        CONF_CO2,
        CONF_PM25,
        CONF_TEMP,
        CONF_RH,
        CONF_PM1,
        CONF_PM4,
        CONF_PM10,
        CONF_VOC,
        CONF_NOX,
    ]:
        s = await cg.get_variable(config[key])
        cg.add(getattr(var, f"set_{key}")(s))

    # Optional: wire up the update entity if configured
    if CONF_FW_UPDATE in config:
        fw = await cg.get_variable(config[CONF_FW_UPDATE])
        cg.add(var.set_fw_update(fw))
