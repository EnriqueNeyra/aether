import os
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import sensor, web_server_base, update, switch

# Make sure web_server_base and update core are loaded
AUTO_LOAD = ["web_server_base", "update"]

_BASE_DIR = os.path.dirname(__file__)
_WEB_DIR = os.path.join(_BASE_DIR, 'web')


def _generate_html_header():
    """Read web/ sources, inline CSS/JS, and write the C++ header."""
    with open(os.path.join(_WEB_DIR, 'index.html'), 'r') as f:
        html = f.read()
    with open(os.path.join(_WEB_DIR, 'style.css'), 'r') as f:
        css = f.read()
    with open(os.path.join(_WEB_DIR, 'app.js'), 'r') as f:
        js = f.read()

    html = html.replace('<link rel="stylesheet" href="style.css">', f'<style>\n{css}\n</style>')
    html = html.replace('<script src="app.js"></script>', f'<script>\n{js}\n</script>')

    header = f'static const char INDEX_HTML[] = R"HTML({html})HTML";\n'
    with open(os.path.join(_BASE_DIR, 'aether_ui_html.h'), 'w') as f:
        f.write(header)


# Generate the inlined HTML header before compilation
_generate_html_header()

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
CONF_TEMP_UNIT_SWITCH = "temp_unit_switch"

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
        cv.Required(CONF_TEMP_UNIT_SWITCH): cv.use_id(switch.Switch),

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

    # Wire up the temp unit switch
    sw = await cg.get_variable(config[CONF_TEMP_UNIT_SWITCH])
    cg.add(var.set_temp_unit_switch(sw))

    if CONF_FW_UPDATE in config:
        fw = await cg.get_variable(config[CONF_FW_UPDATE])
        cg.add(var.set_fw_update(fw))
