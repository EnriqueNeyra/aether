import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..sensor import SEN6XComponent

CONF_MEASUREMENT_RUNNING = "measurement_running"
CONF_SEN6X_ID = "sen6x_id"


CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(device_class="running").extend(
    {
        cv.Required(CONF_SEN6X_ID): cv.use_id(SEN6XComponent),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_SEN6X_ID])
    var = await binary_sensor.new_binary_sensor(config)
    cg.add(parent.set_measurement_running_binary_sensor(var))
