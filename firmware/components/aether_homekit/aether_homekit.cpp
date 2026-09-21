#include "aether_homekit.h"

// The only translation unit that may include HomeSpan. See the note at the top
// of aether_homekit.h for why this is kept out of the header.
#include <HomeSpan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>

namespace aether
{

  static const char *const TAG = "aether_homekit";

  // HAP AirQuality values (characteristic 0x95).
  enum : uint8_t
  {
    HK_AQ_UNKNOWN = 0,
    HK_AQ_EXCELLENT = 1,
    HK_AQ_GOOD = 2,
    HK_AQ_FAIR = 3,
    HK_AQ_INFERIOR = 4,
    HK_AQ_POOR = 5
  };

  /// Holds every HomeSpan type, so none of them appear in the header.
  /// Characteristics are owned by HomeSpan once constructed.
  struct AetherHomeKit::Impl
  {
    SpanCharacteristic *temp{nullptr};
    SpanCharacteristic *rh{nullptr};
    SpanCharacteristic *co2_level{nullptr};
    SpanCharacteristic *co2_detected{nullptr};
    SpanCharacteristic *air_quality{nullptr};
    SpanCharacteristic *pm25{nullptr};
    SpanCharacteristic *pm10{nullptr};
    SpanCharacteristic *voc{nullptr};
    SpanCharacteristic *nox{nullptr};
  };

  float AetherHomeKit::get_setup_priority() const
  {
    // Deliberately *above* setup_priority::WIFI (250) so reclaim_wifi_stack_()
    // runs before ESPHome's WiFi component initialises. HomeSpan's begin() does
    // not need a network, so there is nothing to gain by starting later.
    return esphome::setup_priority::WIFI + 1.0f;
  }

  void AetherHomeKit::add_accessory_info_(const char *name, const char *serial_suffix)
  {
    // Every accessory in a bridge needs its own AccessoryInformation service,
    // and HAP requires it to be the first service defined on that accessory.
    char serial[sizeof(serial_number_) + 4];
    snprintf(serial, sizeof(serial), "%s%s", serial_number_, serial_suffix);

    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Name(name);
    new Characteristic::Manufacturer(manufacturer_);
    new Characteristic::Model(model_);
    new Characteristic::SerialNumber(serial);
#ifdef ESPHOME_PROJECT_VERSION
    new Characteristic::FirmwareRevision(ESPHOME_PROJECT_VERSION);
#endif
  }

  void AetherHomeKit::reclaim_wifi_stack_()
  {
    // HomeSpan overrides arduino-esp32's weak init() hook, which runs before
    // ESPHome's setup(). Span::init() calls WiFi.mode(WIFI_STA), which brings up
    // the default event loop, both default WiFi netifs and esp_wifi. ESPHome's
    // wifi_pre_setup_() then gets ESP_ERR_INVALID_STATE from
    // esp_event_loop_create_default() and *returns early*, so it never registers
    // its event handlers, never creates its netifs and never calls
    // esp_wifi_init(). The symptom is a device with no setup AP and no STA.
    //
    // Neither ordering can be changed (init() runs before any component), so
    // undo arduino's half of it here and hand ESPHome a clean stack. Tear down
    // in dependency order: wifi, then netifs, then the event loop.
    esp_wifi_stop();
    esp_wifi_deinit();

    // ESPHome calls esp_netif_create_default_wifi_{sta,ap}(), which assert() if
    // an interface with the same key already exists.
    for (const char *key : {"WIFI_STA_DEF", "WIFI_AP_DEF"})
    {
      esp_netif_t *netif = esp_netif_get_handle_from_ifkey(key);
      if (netif != nullptr)
        esp_netif_destroy_default_wifi(netif);
    }

    esp_event_loop_delete_default();

    ESP_LOGD(TAG, "Reclaimed WiFi stack from HomeSpan for ESPHome");
  }

  void AetherHomeKit::refresh_paired_()
  {
    bool paired = false;
    for (auto it = homeSpan.controllerListBegin(); it != homeSpan.controllerListEnd(); ++it)
    {
      if (it->isAdmin())
      {
        paired = true;
        break;
      }
    }
    paired_ = paired;
  }

  void AetherHomeKit::setup()
  {
    reclaim_wifi_stack_();

    impl_ = new Impl();

    last_temp_ = last_rh_ = last_co2_ = NAN;
    last_pm25_ = last_pm10_ = last_voc_ = last_nox_ = NAN;

    // ESPHome's web_server already owns port 80, which is HomeSpan's default.
    homeSpan.setPortNum(port_);

    // ESPHome owns the serial port; HomeSpan must neither read the CLI from it
    // nor scribble its own logs across ESPHome's.
    homeSpan.setSerialInputDisable(true);
    homeSpan.setLogLevel(-1);

    // ESPHome owns WiFi. Neutralise HomeSpan's connect path so it can never
    // call WiFi.begin() behind ESPHome's back. HomeSpan still learns it is
    // online from the arduino GOT_IP event and starts its HAP server then.
    homeSpan.setWifiBegin([](const char *, const char *) {});

    // Share ESPHome's hostname so both stacks advertise one identity over mDNS.
    homeSpan.setHostNameSuffix("");
    homeSpan.setQRID(setup_id_);

    const std::string host = esphome::App.get_name();
    snprintf(serial_number_, sizeof(serial_number_), "%s",
             esphome::get_mac_address().c_str());

    // Published as a HAP *bridge* rather than one accessory with four sensor
    // services. The Home app collapses a multi-service accessory into a single
    // tile - which surfaced only the CO2 alarm and hid every reading - whereas
    // each bridged accessory gets its own tile showing its value, and can be
    // picked individually in the Home widget. HomeSpan infers bridge mode from
    // accessory 1 containing nothing but AccessoryInformation.
    homeSpan.begin(Category::Bridges, accessory_name_, host.c_str(), model_);

    snprintf(name_temp_, sizeof(name_temp_), "%s Temperature", accessory_name_);
    snprintf(name_rh_, sizeof(name_rh_), "%s Humidity", accessory_name_);
    snprintf(name_co2_, sizeof(name_co2_), "%s CO2", accessory_name_);
    snprintf(name_aq_, sizeof(name_aq_), "%s Air Quality", accessory_name_);

    // Accessory 1: the bridge itself.
    new SpanAccessory();
    add_accessory_info_(accessory_name_, "");

    // Accessory 2: temperature.
    new SpanAccessory();
    add_accessory_info_(name_temp_, "-T");
    new Service::TemperatureSensor();
    // HAP defaults CurrentTemperature to 0..100 C; the SEN66 reads below zero.
    impl_->temp = (new Characteristic::CurrentTemperature(20.0f))->setRange(-40, 100);

    // Accessory 3: humidity.
    new SpanAccessory();
    add_accessory_info_(name_rh_, "-H");
    new Service::HumiditySensor();
    impl_->rh = new Characteristic::CurrentRelativeHumidity(50.0f);

    // Accessory 4: CO2.
    new SpanAccessory();
    add_accessory_info_(name_co2_, "-C");
    new Service::CarbonDioxideSensor();
    impl_->co2_detected = new Characteristic::CarbonDioxideDetected(0);
    impl_->co2_level = (new Characteristic::CarbonDioxideLevel(400.0f))->setRange(0, 100000);

    // Accessory 5: air quality, carrying every particulate/gas reading.
    new SpanAccessory();
    add_accessory_info_(name_aq_, "-A");
    new Service::AirQualitySensor();
    impl_->air_quality = new Characteristic::AirQuality(HK_AQ_UNKNOWN);
    impl_->pm25 = (new Characteristic::PM25Density(0.0f))->setRange(0, 1000);
    impl_->pm10 = (new Characteristic::PM10Density(0.0f))->setRange(0, 1000);
    // Sensirion's VOC Index is a unitless 1-500 scale, not a ug/m3 density.
    // VOCDensity is the only numeric slot HAP offers, so the Home app labels
    // the index with density units.
    impl_->voc = (new Characteristic::VOCDensity(0.0f))->setRange(0, 1000);

    if (nox_ != nullptr)
    {
      // Same caveat as VOC: NOx Index is 1-500 and unitless, but
      // NitrogenDioxideDensity is the closest thing HAP defines.
      impl_->nox = (new Characteristic::NitrogenDioxideDensity(0.0f))->setRange(0, 1000);
    }

    apply_pairing_code_();

    snprintf(pairing_code_display_, sizeof(pairing_code_display_), "%.3s-%.2s-%.3s",
             pairing_code_, pairing_code_ + 3, pairing_code_ + 5);

    // homeSpan.qrCode is private, so build the X-HM payload from our own HapQR.
    HapQR qr;
    snprintf(qr_payload_, sizeof(qr_payload_), "%s",
             qr.get(atoi(pairing_code_), setup_id_,
                    static_cast<uint8_t>(Category::Sensors)));

    // Safe to read unlocked: HomeSpan's poll task does not exist yet.
    refresh_paired_();

    // Pairing runs SRP-3072, which takes seconds of solid CPU. Running it on
    // HomeSpan's own task keeps it off ESPHome's loop, where it would starve
    // the e-paper render and trip the watchdog.
    homeSpan.autoPoll(12288, 1, 0);

    started_ = true;
    ESP_LOGI(TAG, "HomeKit ready on port %u, setup code %s", port_, pairing_code_display_);
  }

  void AetherHomeKit::apply_pairing_code_()
  {
    // setPairingCode() always regenerates SRP verification data, which is
    // seconds of modular exponentiation. Only pay that when the configured code
    // actually differs from the one already committed to NVS.
    const uint32_t desired = esphome::fnv1_hash(pairing_code_);
    pairing_pref_ = esphome::global_preferences->make_preference<uint32_t>(
        esphome::fnv1_hash("aether_homekit_pairing_code"));

    uint32_t stored = 0;
    if (pairing_pref_.load(&stored) && stored == desired)
      return;

    ESP_LOGI(TAG, "Applying new HomeKit pairing code (one-time SRP setup, takes a few seconds)");
    homeSpan.setPairingCode(pairing_code_);
    pairing_pref_.save(&desired);
  }

  uint8_t AetherHomeKit::compute_air_quality_(float pm25, float voc, float co2) const
  {
    uint8_t worst = HK_AQ_UNKNOWN;

    // PM2.5 bands follow the WHO/EPA-style breakpoints Home app users expect.
    if (!std::isnan(pm25))
    {
      const uint8_t band = pm25 <= 10.0f   ? HK_AQ_EXCELLENT
                           : pm25 <= 20.0f ? HK_AQ_GOOD
                           : pm25 <= 25.0f ? HK_AQ_FAIR
                           : pm25 <= 50.0f ? HK_AQ_INFERIOR
                                           : HK_AQ_POOR;
      worst = std::max(worst, band);
    }

    // Sensirion VOC Index: 100 is the learned baseline, higher is worse.
    if (!std::isnan(voc))
    {
      const uint8_t band = voc <= 50.0f    ? HK_AQ_EXCELLENT
                           : voc <= 100.0f ? HK_AQ_GOOD
                           : voc <= 150.0f ? HK_AQ_FAIR
                           : voc <= 250.0f ? HK_AQ_INFERIOR
                                           : HK_AQ_POOR;
      worst = std::max(worst, band);
    }

    if (!std::isnan(co2))
    {
      const uint8_t band = co2 <= 800.0f    ? HK_AQ_EXCELLENT
                           : co2 <= 1000.0f ? HK_AQ_GOOD
                           : co2 <= 1500.0f ? HK_AQ_FAIR
                           : co2 <= 2000.0f ? HK_AQ_INFERIOR
                                            : HK_AQ_POOR;
      worst = std::max(worst, band);
    }

    return worst;
  }

  /// Pushes `value` only if it moved by more than `epsilon`, so a stable
  /// reading does not generate a HomeKit notification every cycle.
  static void push_if_changed(SpanCharacteristic *c, float *cache, float value,
                              float epsilon)
  {
    if (c == nullptr || std::isnan(value))
      return;
    if (!std::isnan(*cache) && std::fabs(*cache - value) < epsilon)
      return;
    *cache = value;
    c->setVal(value);
  }

  void AetherHomeKit::push_values_()
  {
    const float temp = temp_ != nullptr ? temp_->state : NAN;
    const float rh = rh_ != nullptr ? rh_->state : NAN;
    const float co2 = co2_ != nullptr ? co2_->state : NAN;
    const float pm25 = pm25_ != nullptr ? pm25_->state : NAN;
    const float pm10 = pm10_ != nullptr ? pm10_->state : NAN;
    const float voc = voc_ != nullptr ? voc_->state : NAN;
    const float nox = nox_ != nullptr ? nox_->state : NAN;

    push_if_changed(impl_->temp, &last_temp_, temp, 0.1f);
    push_if_changed(impl_->rh, &last_rh_, rh, 0.5f);
    push_if_changed(impl_->co2_level, &last_co2_, co2, 5.0f);
    push_if_changed(impl_->pm25, &last_pm25_, pm25, 0.3f);
    push_if_changed(impl_->pm10, &last_pm10_, pm10, 0.3f);
    push_if_changed(impl_->voc, &last_voc_, voc, 1.0f);
    push_if_changed(impl_->nox, &last_nox_, nox, 1.0f);

    if (impl_->co2_detected != nullptr && !std::isnan(co2))
    {
      const bool detected = co2 >= co2_threshold_;
      if (detected != last_co2_detected_)
      {
        last_co2_detected_ = detected;
        impl_->co2_detected->setVal(detected ? 1 : 0);
      }
    }

    if (impl_->air_quality != nullptr)
    {
      const uint8_t aq = compute_air_quality_(pm25, voc, co2);
      if (aq != HK_AQ_UNKNOWN && aq != last_air_quality_)
      {
        last_air_quality_ = aq;
        impl_->air_quality->setVal(aq);
      }
    }
  }

  void AetherHomeKit::bridge_wifi_state_()
  {
    // ESPHome owns esp_wifi now, so arduino's WiFi event translation layer is
    // never registered and HomeSpan would never see the GOT_IP that makes it
    // start mDNS advertising and its HAP server. Feed it ESPHome's connection
    // state through arduino's event queue, which HomeSpan's callbacks sit on.
    //
    // The *_ETH_* events are used rather than the WiFi ones on purpose: they
    // drive the identical connected/disconnected path in Span::networkCallback
    // (connected++, then configureNetwork()), but read ETH.localIP() instead of
    // WiFi.localIP(). arduino's WiFi netif pointer dangles after
    // reclaim_wifi_stack_() destroyed it, whereas ETH's is genuinely null and
    // safely guarded. Nothing here enables HomeSpan's Ethernet mode, which is
    // gated on a separate ARDUINO_EVENT_ETH_START we never post.
    auto *wifi = esphome::wifi::global_wifi_component;
    const bool connected = wifi != nullptr && wifi->is_connected();
    if (connected == wifi_connected_)
      return;
    wifi_connected_ = connected;

    arduino_event_t event{};
    event.event_id = connected ? ARDUINO_EVENT_ETH_GOT_IP : ARDUINO_EVENT_ETH_DISCONNECTED;
    Network.postEvent(&event);
    ESP_LOGI(TAG, "WiFi %s; notified HomeKit", connected ? "connected" : "disconnected");
  }

  void AetherHomeKit::loop()
  {
    if (!started_)
      return;

    bridge_wifi_state_();

    // The SEN66 only produces new data every 5s; matching that avoids taking
    // HomeSpan's lock on every ESPHome loop iteration for nothing.
    const uint32_t now = esphome::millis();
    if (now - last_push_ms_ < 5000)
      return;
    last_push_ms_ = now;

    // HomeSpan polls on its own task, so characteristic writes from this task
    // must be made under its lock. The macro releases at end of scope.
    homeSpanPAUSE;
    push_values_();
    refresh_paired_();
  }

  void AetherHomeKit::dump_config()
  {
    ESP_LOGCONFIG(TAG, "Aether HomeKit:");
    ESP_LOGCONFIG(TAG, "  Accessory: %s (%s %s)", accessory_name_, manufacturer_, model_);
    ESP_LOGCONFIG(TAG, "  HAP port: %u", port_);
    ESP_LOGCONFIG(TAG, "  Setup ID: %s", setup_id_);
    ESP_LOGCONFIG(TAG, "  Pairing code: %s", pairing_code_display_);
    ESP_LOGCONFIG(TAG, "  Setup payload: %s", qr_payload_);
    ESP_LOGCONFIG(TAG, "  Paired: %s", is_paired() ? "yes" : "no");
    ESP_LOGCONFIG(TAG, "  CO2 alert threshold: %.0f ppm", co2_threshold_);
  }

} // namespace aether
