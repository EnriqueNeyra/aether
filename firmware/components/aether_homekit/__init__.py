import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_NAME, CONF_PORT
from esphome.components import sensor
from esphome.components.esp32 import add_idf_component

DEPENDENCIES = ["esp32", "wifi"]

# Pinned so a HomeSpan release can never silently change pairing behaviour or
# the arduino-esp32 version it demands. 2.1.8 requires arduino-esp32 >= 3.3.0,
# which ESPHome 2026.2.4 satisfies (it ships 3.3.7).
HOMESPAN_REF = "https://github.com/HomeSpan/HomeSpan.git#2.1.8"

# arduino-esp32 bundled libraries reachable from HomeSpan.h's include tree.
_ARDUINO_LIB_INCLUDES = (
    "Network",
    "WiFi",
    "Ethernet",
    "SPI",
    "FS",
    "Update",
    "ESPmDNS",
    "ArduinoOTA",
    "NetworkClientSecure",
)

aether_ns = cg.esphome_ns.namespace("aether")
AetherHomeKit = aether_ns.class_("AetherHomeKit", cg.Component)

CONF_CO2 = "co2"
CONF_TEMP = "temp"
CONF_RH = "rh"
CONF_PM25 = "pm25"
CONF_PM10 = "pm10"
CONF_VOC = "voc"
CONF_NOX = "nox"
CONF_PAIRING_CODE = "pairing_code"
CONF_SETUP_ID = "setup_id"
CONF_CO2_THRESHOLD = "co2_threshold"
CONF_MODEL = "model"
CONF_MANUFACTURER = "manufacturer"

# HAP forbids these as setup codes; HomeSpan rejects them at runtime and halts,
# so reject them at config time instead where the error is actionable.
_FORBIDDEN_CODES = {
    "00000000",
    "11111111",
    "22222222",
    "33333333",
    "44444444",
    "55555555",
    "66666666",
    "77777777",
    "88888888",
    "99999999",
    "12345678",
    "87654321",
}


def _validate_pairing_code(value):
    """Accept 8 digits, optionally written as 466-37-726."""
    value = cv.string_strict(value)
    digits = value.replace("-", "").replace(" ", "")
    if len(digits) != 8 or not digits.isdigit():
        raise cv.Invalid(
            f"HomeKit pairing code must be exactly 8 digits (got '{value}'). "
            "Write it as '46637726' or '466-37-726'."
        )
    if digits in _FORBIDDEN_CODES:
        raise cv.Invalid(
            f"'{value}' is on Apple's list of disallowed HomeKit setup codes. "
            "Pick a less repetitive 8-digit code."
        )
    return digits


def _validate_setup_id(value):
    """HAP setup IDs are exactly 4 uppercase alphanumeric characters."""
    value = cv.string_strict(value).upper()
    if len(value) != 4 or not value.isalnum():
        raise cv.Invalid(
            f"setup_id must be exactly 4 alphanumeric characters (got '{value}')."
        )
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(AetherHomeKit),
        # Only the metrics HomeKit can actually represent. PM1.0 and PM4.0 are
        # deliberately absent: HAP defines no characteristic for them, so they
        # stay on the e-paper display and the local web UI only.
        cv.Required(CONF_CO2): cv.use_id(sensor.Sensor),
        cv.Required(CONF_TEMP): cv.use_id(sensor.Sensor),
        cv.Required(CONF_RH): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PM25): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PM10): cv.use_id(sensor.Sensor),
        cv.Required(CONF_VOC): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_NOX): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_NAME, default="Aether"): cv.string_strict,
        cv.Optional(CONF_MODEL, default="Aether"): cv.string_strict,
        cv.Optional(CONF_MANUFACTURER, default="Syntropy Labs"): cv.string_strict,
        cv.Optional(CONF_PAIRING_CODE, default="46637726"): _validate_pairing_code,
        cv.Optional(CONF_SETUP_ID, default="AETH"): _validate_setup_id,
        # HomeSpan's HAP server cannot share port 80 with ESPHome's web_server.
        cv.Optional(CONF_PORT, default=1201): cv.port,
        cv.Optional(CONF_CO2_THRESHOLD, default=1000): cv.float_range(min=400, max=5000),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_library("HomeSpan", None, HOMESPAN_REF)

    # HomeSpan's HAP.cpp needs sodium.h for Ed25519/Curve25519/ChaCha20-Poly1305.
    # ESP-IDF 5.5 no longer ships libsodium as a built-in component, so pull it
    # from the Espressif component registry.
    add_idf_component(name="espressif/libsodium", ref="^1.0.20")

    # ESPHome builds arduino-esp32 as an IDF component, which means the bundled
    # arduino libraries HomeSpan includes (WiFi.h, ETH.h, ArduinoOTA.h, ...) get
    # promoted to PlatformIO project libraries. PlatformIO then compiles each of
    # them without its siblings' include paths, so WiFi cannot find Network.h,
    # Ethernet cannot find SPI.h, and so on. Put those paths back explicitly.
    for lib in _ARDUINO_LIB_INCLUDES:
        cg.add_build_flag(
            f"-I$PROJECT_PACKAGES_DIR/framework-arduinoespressif32/libraries/{lib}/src"
        )

    # Lets aether_web_ui and aether_epaper compile with or without HomeKit present.
    cg.add_define("USE_AETHER_HOMEKIT")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    for key in [CONF_CO2, CONF_TEMP, CONF_RH, CONF_PM25, CONF_PM10, CONF_VOC]:
        s = await cg.get_variable(config[key])
        cg.add(getattr(var, f"set_{key}")(s))

    if CONF_NOX in config:
        nox = await cg.get_variable(config[CONF_NOX])
        cg.add(var.set_nox(nox))

    cg.add(var.set_accessory_name(config[CONF_NAME]))
    cg.add(var.set_model(config[CONF_MODEL]))
    cg.add(var.set_manufacturer(config[CONF_MANUFACTURER]))
    cg.add(var.set_pairing_code(config[CONF_PAIRING_CODE]))
    cg.add(var.set_setup_id(config[CONF_SETUP_ID]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_co2_threshold(config[CONF_CO2_THRESHOLD]))
