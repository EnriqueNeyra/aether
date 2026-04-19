#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/update/update_entity.h"

#include <string>
#include <cmath>


namespace aether
{

  using esphome::sensor::Sensor;
  using esphome::switch_::Switch;
  using esphome::update::UpdateEntity;

  static const char *const TAG = "aether_web_ui";

  // Full HTML/JS UI.
  // - Main tab: Environment (live metrics)
  // - Second tab: Firmware & Updates (one-click managed update via http_request)
  #include "aether_web_ui_html.h"

  class AetherWebUI : public esphome::Component, public AsyncWebHandler
  {
  public:
    void set_co2(Sensor *s) { co2_ = s; }
    void set_pm25(Sensor *s) { pm25_ = s; }
    void set_temp(Sensor *s) { temp_ = s; }
    void set_rh(Sensor *s) { rh_ = s; }
    void set_pm1(Sensor *s) { pm1_ = s; }
    void set_pm4(Sensor *s) { pm4_ = s; }
    void set_pm10(Sensor *s) { pm10_ = s; }
    void set_voc(Sensor *s) { voc_ = s; }
    void set_nox(Sensor *s) { nox_ = s; }

    void set_fw_update(UpdateEntity *u) { fw_update_ = u; }
    void set_temp_unit_switch(Switch *s) { temp_unit_switch_ = s; }

    void setup() override
    {
      auto *ws = esphome::web_server_base::global_web_server_base;
      if (ws == nullptr)
      {
        ESP_LOGW(TAG, "web_server_base not initialized; UI will not be available");
        return;
      }
      ws->add_handler(this);
      ESP_LOGI(TAG, "Aether UI handler registered");
    }

    void dump_config() override
    {
      ESP_LOGCONFIG(TAG, "Aether custom web UI");
#ifdef ESPHOME_PROJECT_VERSION
      ESP_LOGCONFIG(TAG, "  Firmware version: %s", ESPHOME_PROJECT_VERSION);
#else
      ESP_LOGCONFIG(TAG, "  Firmware version: unknown");
#endif
      ESP_LOGCONFIG(TAG, "  http_request update wired: %s", fw_update_ != nullptr ? "YES" : "NO");
    }

    // AsyncWebHandler interface
    bool canHandle(AsyncWebServerRequest *request) const override
    {
      auto url = request->url();
      if (url == "/" || url == "/index.html")
        return true;
      if (url.rfind("/api/", 0) == 0)
        return true;
      return false;
    }

    void handleRequest(AsyncWebServerRequest *request) override
    {
      auto url = request->url();
      if (url == "/" || url == "/index.html")
      {
        handle_root_(request);
      }
      else if (url == "/api/state")
      {
        handle_state_(request);
      }
      else if (url == "/api/perform_update")
      {
        handle_perform_update_(request);
      }
      else if (url == "/api/temp_unit")
      {
        handle_temp_unit_(request);
      }
      else
      {
        request->send(404, "text/plain", "Not found");
      }
    }

    bool isRequestHandlerTrivial() const override { return false; }

  private:
    Sensor *co2_{nullptr};
    Sensor *pm25_{nullptr};
    Sensor *temp_{nullptr};
    Sensor *rh_{nullptr};
    Sensor *pm1_{nullptr};
    Sensor *pm4_{nullptr};
    Sensor *pm10_{nullptr};
    Sensor *voc_{nullptr};
    Sensor *nox_{nullptr};

    UpdateEntity *fw_update_{nullptr};
    Switch *temp_unit_switch_{nullptr};

    static bool has_value_(Sensor *s)
    {
      return s != nullptr && !std::isnan(s->state);
    }

    // Append a sensor value as a JSON number or null.
    static int append_sensor_(char *buf, int remaining, const char *key, Sensor *s, const char *fmt)
    {
      if (remaining <= 0)
        return 0;
      if (has_value_(s))
      {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), fmt, s->state);
        return snprintf(buf, remaining, "\"%s\":%s", key, tmp);
      }
      return snprintf(buf, remaining, "\"%s\":null", key);
    }

    void handle_root_(AsyncWebServerRequest *request)
    {
      request->send(200, "text/html", INDEX_HTML);
    }

    void handle_state_(AsyncWebServerRequest *request)
    {
      char json[800];
      const char *ver =
#ifdef ESPHOME_PROJECT_VERSION
          ESPHOME_PROJECT_VERSION;
#else
          "unknown";
#endif

      const bool use_f = temp_unit_switch_ != nullptr ? !temp_unit_switch_->state : true;

      bool has_update = false;
      const char *latest_version = "";
      const char *update_state_str = "unknown";
      float update_progress = 0.0f;

      if (fw_update_ != nullptr)
      {
        has_update = fw_update_->state == esphome::update::UPDATE_STATE_AVAILABLE;
        latest_version = fw_update_->update_info.latest_version.c_str();
        if (fw_update_->state == esphome::update::UPDATE_STATE_NO_UPDATE)
            update_state_str = "no_update";
        else if (fw_update_->state == esphome::update::UPDATE_STATE_AVAILABLE)
            update_state_str = "available";
        else if (fw_update_->state == esphome::update::UPDATE_STATE_INSTALLING)
            update_state_str = "installing";

        if (fw_update_->update_info.has_progress)
            update_progress = fw_update_->update_info.progress;
      }

      // Build JSON incrementally so each sensor emits null when unavailable.
      char *p = json;
      int rem = sizeof(json);
      int n;

      n = snprintf(p, rem, "{"); p += n; rem -= n;

      struct { const char *key; Sensor *s; const char *fmt; } sensors[] = {
        {"co2",  co2_,  "%.1f"}, {"temp", temp_, "%.2f"}, {"rh",   rh_,   "%.2f"},
        {"pm1",  pm1_,  "%.1f"}, {"pm25", pm25_, "%.1f"}, {"pm4",  pm4_,  "%.1f"},
        {"pm10", pm10_, "%.1f"}, {"voc",  voc_,  "%.1f"}, {"nox",  nox_,  "%.1f"},
      };
      for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++)
      {
        n = append_sensor_(p, rem, sensors[i].key, sensors[i].s, sensors[i].fmt);
        p += n; rem -= n;
        n = snprintf(p, rem, ","); p += n; rem -= n;
      }

      n = snprintf(p, rem,
               "\"fw_version\":\"%.48s\","
               "\"temp_unit\":\"%s\","
               "\"has_update\":%s,"
               "\"latest_version\":\"%.48s\","
               "\"update_state\":\"%s\","
               "\"update_progress\":%.2f}",
               ver,
               use_f ? "F" : "C",
               has_update ? "true" : "false",
               latest_version,
               update_state_str,
               update_progress);
      p += n; rem -= n;

      if (rem <= 0)
      {
        ESP_LOGW(TAG, "JSON response truncated");
        request->send(500, "application/json", "{\"error\":\"response_truncated\"}");
        return;
      }

      request->send(200, "application/json", json);
    }


    void handle_perform_update_(AsyncWebServerRequest *request)
    {
      if (request->method() != HTTP_POST)
      {
        request->send(405, "application/json",
                      "{\"ok\":false,\"error\":\"method_not_allowed\"}");
        return;
      }
      if (fw_update_ == nullptr)
      {
        ESP_LOGW(TAG, "Update requested but http_request update component is not wired");
        request->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"update_not_configured\"}");
        return;
      }

      ESP_LOGI(TAG, "Starting http_request firmware update via web UI");
      // false = do not force if it thinks there is no update; UI already checks versions
      fw_update_->perform(false);
      request->send(200, "application/json", "{\"ok\":true}");
    }

    void handle_temp_unit_(AsyncWebServerRequest *request)
    {
      if (request->method() != HTTP_POST)
      {
        request->send(405, "application/json",
                      "{\"ok\":false,\"error\":\"method_not_allowed\"}");
        return;
      }
      if (temp_unit_switch_ == nullptr)
      {
        ESP_LOGW(TAG, "Temperature unit change requested but switch is not wired");
        request->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"temp_unit_not_configured\"}");
        return;
      }

      if (!request->hasParam("unit"))
      {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing_unit\"}");
        return;
      }

      const std::string unit = request->getParam("unit")->value();
      if (unit == "C")
      {
        temp_unit_switch_->turn_on();
        request->send(200, "application/json",
                      "{\"ok\":true,\"temp_unit\":\"C\"}");
      }
      else if (unit == "F")
      {
        temp_unit_switch_->turn_off();
        request->send(200, "application/json",
                      "{\"ok\":true,\"temp_unit\":\"F\"}");
      }
      else
      {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid_unit\"}");
      }
    }
  };

} // namespace aether
