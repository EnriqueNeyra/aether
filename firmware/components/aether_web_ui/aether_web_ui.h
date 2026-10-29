#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/string_ref.h"
#include "esphome/components/select/select.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/update/update_entity.h"

#ifdef USE_AETHER_HOMEKIT
#include "esphome/components/aether_homekit/aether_homekit.h"
#endif
#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif

#include <string>
#include <cmath>
#include <cstring>
#include <functional>


namespace aether
{

  using esphome::sensor::Sensor;
  using esphome::select::Select;
  using esphome::update::UpdateEntity;

  static const char *const TAG = "aether_web_ui";

  // Full HTML/JS UI.
  // - Main tab: Environment (live metrics)
  // - Second tab: Firmware & Updates (one-click managed update via http_request)
  #include "aether_web_ui_html.h"

  // Requests are served on the ESP-IDF httpd task, not ESPHome's main loop.
  // Handlers therefore never change entities directly: actions are deferred to
  // the main loop (as ESPHome's own web_server does), and update/unit status is
  // read from a snapshot that loop() refreshes under a lock.
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

#ifdef USE_AETHER_HOMEKIT
    void set_homekit(AetherHomeKit *hk) { homekit_ = hk; }
#endif

    void set_fw_update(UpdateEntity *u) { fw_update_ = u; }
    void set_temp_unit_switch(Select *s) { temp_unit_select_ = s; }
    void set_on_check_update(std::function<void()> f) { on_check_update_ = f; }

    void setup() override
    {
      // The HTTP listener itself is started by web_server (a declared
      // dependency of this component); we only attach a handler to it.
      auto *ws = esphome::web_server_base::global_web_server_base;
      if (ws == nullptr)
      {
        ESP_LOGW(TAG, "web_server_base not initialized; UI will not be available");
        return;
      }
      ws->add_handler(this);
      ESP_LOGI(TAG, "Aether UI handler registered");
    }

    void loop() override
    {
      Snapshot next;
      next.use_f = temp_unit_select_ == nullptr || temp_unit_select_->current_option() != "Celsius";
      if (fw_update_ != nullptr)
      {
        next.update_state = fw_update_->state;
        if (fw_update_->update_info.has_progress)
          next.update_progress = fw_update_->update_info.progress;
        snprintf(next.latest_version, sizeof(next.latest_version), "%s",
                 fw_update_->update_info.latest_version.c_str());
      }

      esphome::LockGuard lock(snapshot_lock_);
      // http_request drops back to AVAILABLE when an install aborts or fails;
      // a successful install reboots instead, so that transition is a failure.
      if (snapshot_.update_state == esphome::update::UPDATE_STATE_INSTALLING &&
          next.update_state == esphome::update::UPDATE_STATE_AVAILABLE)
        next.install_failed = true;
      else if (next.update_state != esphome::update::UPDATE_STATE_INSTALLING)
        next.install_failed = snapshot_.install_failed;
      snapshot_ = next;
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
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      const esphome::StringRef url = request->url_to(url_buf);
      if (url == "/" || url == "/index.html")
        return !captive_portal_active_();
      if (url.starts_with("/api/"))
        return true;
      return false;
    }

    void handleRequest(AsyncWebServerRequest *request) override
    {
      char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
      const esphome::StringRef url = request->url_to(url_buf);
      if (url == "/" || url == "/index.html")
      {
        handle_root_(request);
        return;
      }

      // Browsers send Origin on cross-site requests; refuse those so other web
      // pages can't read or drive the device, as ESPHome's web_server does.
      // Refusals use 400: ESPHome's ESP32 server only sends 200, 204, 400, 401,
      // 404, 409 and 422, and turns any other code (403, 405) into a 500.
      if (!origin_allowed_(request))
      {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"forbidden_origin\"}");
        return;
      }

      if (url == "/api/state")
      {
        handle_state_(request);
      }
      else if (url == "/api/perform_update")
      {
        handle_perform_update_(request);
      }
      else if (url == "/api/check_update")
      {
        handle_check_update_(request);
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
    struct Snapshot
    {
      esphome::update::UpdateState update_state{esphome::update::UPDATE_STATE_UNKNOWN};
      float update_progress{0.0f};
      char latest_version[48]{};
      bool install_failed{false};
      bool use_f{true};
    };

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
#ifdef USE_AETHER_HOMEKIT
    AetherHomeKit *homekit_{nullptr};
#endif
    Select *temp_unit_select_{nullptr};
    std::function<void()> on_check_update_{nullptr};

    esphome::Mutex snapshot_lock_;
    Snapshot snapshot_;

    Snapshot snapshot_copy_()
    {
      esphome::LockGuard lock(snapshot_lock_);
      return snapshot_;
    }

    static bool has_value_(Sensor *s)
    {
      return s != nullptr && std::isfinite(s->state);
    }

    // While the setup AP is up, "/" belongs to the captive portal's Wi-Fi form.
    // Our handler registers first, so without this it would shadow the form for
    // anyone who browses to the AP address directly.
    static bool captive_portal_active_()
    {
#ifdef USE_CAPTIVE_PORTAL
      return esphome::captive_portal::global_captive_portal != nullptr &&
             esphome::captive_portal::global_captive_portal->is_active();
#else
      return false;
#endif
    }

    // Same rule as ESPHome's web_server: allow requests without an Origin
    // (curl, scripts, same-origin GETs) and those whose Origin authority equals
    // the Host they were sent to (the device's IP or mDNS name).
    static bool origin_allowed_(AsyncWebServerRequest *request)
    {
      const auto origin_header = request->get_header("Origin");
      if (!origin_header.has_value() || origin_header->empty())
        return true;
      const std::string &origin = *origin_header;
      const size_t scheme_sep = origin.find("://");
      if (scheme_sep == std::string::npos)
        return false;
      const auto host_header = request->get_header("Host");
      if (!host_header.has_value() || host_header->empty())
        return false;
      const std::string &host = *host_header;
      const size_t authority = scheme_sep + 3;
      return origin.size() - authority == host.size() &&
             memcmp(origin.data() + authority, host.data(), host.size()) == 0;
    }

    // Append a sensor value as a JSON number or null.
    // Returns the number of characters actually written (clamped to remaining).
    static int append_sensor_(char *buf, int remaining, const char *key, Sensor *s, const char *fmt)
    {
      if (remaining <= 0)
        return 0;
      int n;
      if (has_value_(s))
      {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), fmt, s->state);
        n = snprintf(buf, remaining, "\"%s\":%s", key, tmp);
      }
      else
      {
        n = snprintf(buf, remaining, "\"%s\":null", key);
      }
      return n < remaining ? n : remaining - 1;
    }

    // Escape a string for safe JSON insertion: escapes " and \, drops control
    // characters, and stops before a character (or its escape) would not fit.
    static void json_escape_(char *dst, size_t dst_size, const char *src)
    {
      size_t di = 0;
      for (const char *s = src; *s; ++s)
      {
        if (static_cast<unsigned char>(*s) < 0x20)
          continue;
        const bool escape = (*s == '"' || *s == '\\');
        if (di + (escape ? 2 : 1) >= dst_size)
          break;
        if (escape)
          dst[di++] = '\\';
        dst[di++] = *s;
      }
      dst[di] = '\0';
    }

    void handle_root_(AsyncWebServerRequest *request)
    {
      request->send(200, "text/html", INDEX_HTML);
    }

    void handle_state_(AsyncWebServerRequest *request)
    {
      char json[1024];
      const char *ver =
#ifdef ESPHOME_PROJECT_VERSION
          ESPHOME_PROJECT_VERSION;
#else
          "unknown";
#endif

      const Snapshot snap = snapshot_copy_();

      const char *update_state_str = "unknown";
      if (snap.update_state == esphome::update::UPDATE_STATE_NO_UPDATE)
        update_state_str = "no_update";
      else if (snap.update_state == esphome::update::UPDATE_STATE_AVAILABLE)
        update_state_str = "available";
      else if (snap.update_state == esphome::update::UPDATE_STATE_INSTALLING)
        update_state_str = "installing";

      // Build JSON incrementally so each sensor emits null when unavailable.
      // After each snprintf, clamp the advance to avoid underflowing rem, and
      // remember if anything was cut off.
      char *p = json;
      int rem = sizeof(json);
      int n;
      bool truncated = false;

      auto advance = [&](int written) {
        if (written < 0 || written >= rem)
          truncated = true;
        int clamped = (written < rem) ? written : rem - 1;
        if (clamped < 0) clamped = 0;
        p += clamped;
        rem -= clamped;
      };

      n = snprintf(p, rem, "{"); advance(n);

      struct { const char *key; Sensor *s; const char *fmt; } sensors[] = {
        {"co2",  co2_,  "%.1f"}, {"temp", temp_, "%.2f"}, {"rh",   rh_,   "%.2f"},
        {"pm1",  pm1_,  "%.1f"}, {"pm25", pm25_, "%.1f"}, {"pm4",  pm4_,  "%.1f"},
        {"pm10", pm10_, "%.1f"}, {"voc",  voc_,  "%.1f"}, {"nox",  nox_,  "%.1f"},
      };
      for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++)
      {
        n = append_sensor_(p, rem, sensors[i].key, sensors[i].s, sensors[i].fmt);
        advance(n);
        n = snprintf(p, rem, ","); advance(n);
      }

      char escaped_version[64];
      json_escape_(escaped_version, sizeof(escaped_version), snap.latest_version);

      n = snprintf(p, rem,
               "\"fw_version\":\"%.48s\","
               "\"temp_unit\":\"%s\","
               "\"has_update\":%s,"
               "\"latest_version\":\"%s\","
               "\"update_state\":\"%s\","
               "\"update_progress\":%.2f,"
               "\"install_failed\":%s",
               ver,
               snap.use_f ? "F" : "C",
               snap.update_state == esphome::update::UPDATE_STATE_AVAILABLE ? "true" : "false",
               escaped_version,
               update_state_str,
               snap.update_progress,
               snap.install_failed ? "true" : "false");
      advance(n);

#ifdef USE_AETHER_HOMEKIT
      if (homekit_ != nullptr && homekit_->is_ready())
      {
        n = snprintf(p, rem,
                     ",\"homekit\":{\"enabled\":true,\"paired\":%s,"
                     "\"code\":\"%.11s\",\"payload\":\"%.23s\"}",
                     homekit_->is_paired() ? "true" : "false",
                     homekit_->pairing_code_display(),
                     homekit_->qr_payload());
        advance(n);
      }
      else
      {
        n = snprintf(p, rem, ",\"homekit\":{\"enabled\":false}");
        advance(n);
      }
#endif

      n = snprintf(p, rem, "}");
      advance(n);

      if (truncated)
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
        request->send(400, "application/json",
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
      // perform(false) silently does nothing unless an update is available, so
      // say so instead of reporting a start that never happens.
      if (snapshot_copy_().update_state != esphome::update::UPDATE_STATE_AVAILABLE)
      {
        request->send(409, "application/json",
                      "{\"ok\":false,\"error\":\"no_update_available\"}");
        return;
      }

      ESP_LOGI(TAG, "Starting http_request firmware update via web UI");
      this->defer([this]() { fw_update_->perform(false); });
      request->send(200, "application/json", "{\"ok\":true}");
    }

    void handle_check_update_(AsyncWebServerRequest *request)
    {
      if (request->method() != HTTP_POST)
      {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"method_not_allowed\"}");
        return;
      }
      if (fw_update_ == nullptr)
      {
        ESP_LOGW(TAG, "Check update requested but http_request update component is not wired");
        request->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"update_not_configured\"}");
        return;
      }

      ESP_LOGI(TAG, "Triggering firmware update check via web UI");
      if (on_check_update_)
      {
        this->defer([this]() { on_check_update_(); });
      }
      request->send(200, "application/json", "{\"ok\":true}");
    }

    void handle_temp_unit_(AsyncWebServerRequest *request)
    {
      if (request->method() != HTTP_POST)
      {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"method_not_allowed\"}");
        return;
      }
      if (temp_unit_select_ == nullptr)
      {
        ESP_LOGW(TAG, "Temperature unit change requested but select is not wired");
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
      if (unit == "C" || unit == "F")
      {
        const bool celsius = unit == "C";
        this->defer([this, celsius]() {
          temp_unit_select_->make_call().set_option(celsius ? "Celsius" : "Fahrenheit").perform();
        });
        request->send(200, "application/json",
                      celsius ? "{\"ok\":true,\"temp_unit\":\"C\"}" : "{\"ok\":true,\"temp_unit\":\"F\"}");
      }
      else
      {
        request->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid_unit\"}");
      }
    }
  };

} // namespace aether
