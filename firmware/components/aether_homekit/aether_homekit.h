#pragma once

// Deliberately free of any HomeSpan include.
//
// ESPHome pulls every component header into esphome.h, and therefore into all
// translation units. HomeSpan.h drags in <WiFi.h>, <ETH.h>, <ArduinoOTA.h> and
// <esp_now.h>, which makes PlatformIO's library dependency finder promote the
// bundled arduino-esp32 WiFi library to a project library and then fail to
// resolve its Network.h dependency. Keeping the HAP stack behind a pimpl means
// HomeSpan.h is compiled exactly once, in aether_homekit.cpp.

#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/sensor/sensor.h"

namespace aether
{

  class AetherHomeKit : public esphome::Component
  {
  public:
    void set_co2(esphome::sensor::Sensor *s) { co2_ = s; }
    void set_temp(esphome::sensor::Sensor *s) { temp_ = s; }
    void set_rh(esphome::sensor::Sensor *s) { rh_ = s; }
    void set_pm25(esphome::sensor::Sensor *s) { pm25_ = s; }
    void set_pm10(esphome::sensor::Sensor *s) { pm10_ = s; }
    void set_voc(esphome::sensor::Sensor *s) { voc_ = s; }
    void set_nox(esphome::sensor::Sensor *s) { nox_ = s; }

    void set_accessory_name(const char *v) { accessory_name_ = v; }
    void set_model(const char *v) { model_ = v; }
    void set_manufacturer(const char *v) { manufacturer_ = v; }
    void set_pairing_code(const char *v) { pairing_code_ = v; }
    void set_setup_id(const char *v) { setup_id_ = v; }
    void set_port(uint16_t v) { port_ = v; }
    void set_co2_threshold(float v) { co2_threshold_ = v; }

    // ---- Consumed by the e-paper pairing screen and the local web UI ----

    /// Pairing code as stored, e.g. "46637726".
    const char *pairing_code() const { return pairing_code_; }

    /// Pairing code formatted the way the Home app shows it, e.g. "466-37-726".
    const char *pairing_code_display() const { return pairing_code_display_; }

    /// "X-HM://..." setup payload; encode this as a QR code to pair by scanning.
    const char *qr_payload() const { return qr_payload_; }

    /// True once at least one HomeKit controller has completed pairing.
    /// Returns a cached value refreshed under HomeSpan's lock, because the
    /// e-paper render loop polls this at 2 Hz while HomeSpan's own task can be
    /// appending to the controller list mid-pairing.
    bool is_paired() const { return paired_; }

    /// False until setup() has finished bringing the HAP stack up.
    bool is_ready() const { return started_; }

    void setup() override;
    void loop() override;
    void dump_config() override;

    // Must land after WiFi so ESPHome owns the radio and the mDNS responder
    // before HomeSpan attaches its HAP service records to them.
    float get_setup_priority() const override;

  protected:
    struct Impl;

    void apply_pairing_code_();
    void push_values_();

    /// Emits the AccessoryInformation service every bridged accessory requires.
    void add_accessory_info_(const char *name, const char *serial_suffix);

    /// Walks HomeSpan's controller list into `paired_`. Callers must hold
    /// HomeSpan's lock, or run before its poll task is started.
    void refresh_paired_();

    /// Feeds ESPHome's WiFi connection state to HomeSpan as arduino events.
    void bridge_wifi_state_();

    /// Registers the _hap._tcp mDNS service the Home app needs to find us.
    void publish_hap_mdns_();

    /// Undoes the WiFi bring-up HomeSpan performs before ESPHome's setup(), so
    /// ESPHome can initialise the stack itself. Must run before the WiFi
    /// component's setup(); see get_setup_priority().
    void reclaim_wifi_stack_();

    /// HAP AirQuality is a 1-5 ordinal (1 = Excellent, 5 = Poor), not a number.
    uint8_t compute_air_quality_(float pm25, float voc, float co2) const;

    Impl *impl_{nullptr};

    esphome::sensor::Sensor *co2_{nullptr};
    esphome::sensor::Sensor *temp_{nullptr};
    esphome::sensor::Sensor *rh_{nullptr};
    esphome::sensor::Sensor *pm25_{nullptr};
    esphome::sensor::Sensor *pm10_{nullptr};
    esphome::sensor::Sensor *voc_{nullptr};
    esphome::sensor::Sensor *nox_{nullptr};

    const char *accessory_name_{"Aether"};
    const char *model_{"Aether"};
    const char *manufacturer_{"Syntropy Labs"};
    const char *pairing_code_{"46637726"};
    const char *setup_id_{"AETH"};
    uint16_t port_{1201};
    float co2_threshold_{1000.0f};

    char pairing_code_display_[12]{};
    char qr_payload_[24]{};
    char serial_number_[24]{};

    // Per-accessory display names, kept alive for the lifetime of the device.
    char name_temp_[40]{};
    char name_rh_[40]{};
    char name_co2_[40]{};
    char name_aq_[40]{};

    float last_temp_, last_rh_, last_co2_;
    float last_pm25_, last_pm10_, last_voc_, last_nox_;
    uint8_t last_air_quality_{0};
    bool last_co2_detected_{false};

    uint32_t last_push_ms_{0};
    bool started_{false};
    bool paired_{false};
    bool wifi_connected_{false};

    esphome::ESPPreferenceObject pairing_pref_;
  };

} // namespace aether
