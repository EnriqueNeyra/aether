#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Wire.h>
#include <cmath>
#include <cstring>
#include <string>
#include <qrcode.h>

// ESPHome includes
#include "esphome/core/application.h"
#include "esphome/components/wifi/wifi_component.h"

// Layout and Fonts
#include "Inter_Bold12pt7b.h"
#include "Inter_Bold18pt7b.h"
#include "Inter_Bold9pt7b.h"
#include "Inter_Bold_Tabular18pt7b.h"
#include "Inter_Bold_Tabular24pt7b.h"
#include "Satoshi_800_Device_Information_Subset18pt7b.h"
#include "Satoshi_800_Factory_Reset_Subset22pt7b.h"
#include "Satoshi_800_Logo_Subset22pt7b.h"
#include "aether_epaper_layout.h"

namespace aether
{

  namespace aether_epaper
  {

// ==== SPI + Display Pin Configuration ====
#define EPD_MOSI 7
#define EPD_SCK 6
#define EPD_CS 5
#define EPD_DC 4
#define EPD_RST 3
#define EPD_BUSY 1

    // GDEY037T03 (416x240)
    static GxEPD2_BW<GxEPD2_370_GDEY037T03, GxEPD2_370_GDEY037T03::HEIGHT>
        display(GxEPD2_370_GDEY037T03(EPD_CS, EPD_DC, EPD_RST, -1));

    // --- State ---
    enum DisplayMode
    {
      MODE_BOOT,
      MODE_NORMAL,
      MODE_INFO,
      MODE_RESET
    };

    static bool s_inited = false;
    static DisplayMode g_display_mode = MODE_BOOT;
    static bool g_use_f = false;
    static uint8_t g_boot_full_refreshes = 0;
    static uint8_t g_boot_frame = 0;
    static int8_t g_last_info_wifi_state = -1;
    static unsigned long g_last_normal_render_ms = 0;
    static unsigned long g_last_boot_frame_ms = 0;

    // --- Cached Layout Data ---
    struct BootWordmarkLayout
    {
      int baseline_y, left_x, slash_1_x, slash_2_x, right_x;
    };
    static BootWordmarkLayout g_boot_layout = {0, 0, 0, 0, 0};

    // --- Cached readings (stored in °C internally) ---
    static float g_co2 = NAN, g_temp = NAN, g_rh = NAN;
    static float g_pm1 = NAN, g_pm25 = NAN, g_pm4 = NAN, g_pm10 = NAN;
    static float g_voc = NAN, g_nox = NAN;

    // --- Config ---
    static constexpr unsigned long NORMAL_REFRESH_MS = 5000;
    static constexpr unsigned long BOOT_FRAME_MS = 500;
    static constexpr uint8_t BOOT_SLASH_FRAME_COUNT = 4;
    static constexpr const char *INFO_INSTRUCTIONS_URL = "https://github.com/EnriqueNeyra/aether";

    inline void draw_info_qr_card(int x, int y, const char *target);
    inline void draw_info_qr_card(int x, int y, const char *target,
                                  int scale, int padding);
    inline void draw_connected_info_bracket(bool left_side);

    // --- Paged Rendering Helper ---
    template <typename F>
    inline void render_paged(bool full_refresh, F &&draw_func)
    {
      if (full_refresh)
        display.setFullWindow();
      else
        display.setPartialWindow(0, 0, display.width(), display.height());

      display.firstPage();
      do
      {
        draw_func();
        esphome::App.feed_wdt();
      } while (display.nextPage());
    }

    // --- Initialization & Validation ---
    inline void init_once()
    {
      if (s_inited)
        return;

      pinMode(EPD_CS, OUTPUT);
      pinMode(EPD_DC, OUTPUT);
      pinMode(EPD_RST, OUTPUT);
      pinMode(EPD_BUSY, INPUT);

      SPI.begin(EPD_SCK, -1, EPD_MOSI);
      display.init(115200, true, 2, false);
      display.setRotation(1);

      // Calculate boot layout once
      display.setFont(&Satoshi_800_subset22pt7b);
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds("Syntropy // Labs", 0, 0, &x1, &y1, &w, &h);
      int baseline_y = display.height() / 2 - y1 - static_cast<int>(h) / 2;

      auto text_adv = [](const char *text)
      {
        int adv = 0;
        for (const char *p = text; p && *p; ++p)
          adv += Satoshi_800_subset22pt7b.glyph[*p - Satoshi_800_subset22pt7b.first].xAdvance;
        return adv;
      };

      int left_adv = text_adv("Syntropy ");
      int slash_adv = Satoshi_800_subset22pt7b.glyph['/' - Satoshi_800_subset22pt7b.first].xAdvance;
      int right_adv = text_adv(" Labs");
      int total_w = left_adv + (slash_adv * 2) + right_adv;

      int left_x = display.width() / 2 - total_w / 2;
      g_boot_layout = {baseline_y, left_x, left_x + left_adv, left_x + left_adv + slash_adv, left_x + left_adv + (slash_adv * 2)};

      s_inited = true;
    }

    inline bool all_metrics_ready()
    {
      return aether_epaper_layout::has_real_value(g_co2) &&
             aether_epaper_layout::has_real_value(g_temp) &&
             aether_epaper_layout::has_real_value(g_rh) &&
             aether_epaper_layout::has_real_value(g_pm1) &&
             aether_epaper_layout::has_real_value(g_pm25) &&
             aether_epaper_layout::has_real_value(g_pm4) &&
             aether_epaper_layout::has_real_value(g_pm10) &&
             aether_epaper_layout::has_real_value(g_voc) &&
             aether_epaper_layout::has_real_value(g_nox);
    }

    inline void draw_boot_content(uint8_t frame)
    {
      display.setTextColor(GxEPD_BLACK);
      aether_epaper_layout::print_left(display, g_boot_layout.left_x, g_boot_layout.baseline_y, &Satoshi_800_subset22pt7b, "Syntropy ");

      static constexpr const char *SLASHES[4] = {"//", " /", "  ", "/ "};
      const char *s = SLASHES[frame % 4];
      if (s[0] == '/')
        aether_epaper_layout::print_left(display, g_boot_layout.slash_1_x, g_boot_layout.baseline_y, &Satoshi_800_subset22pt7b, "/");
      if (s[1] == '/')
        aether_epaper_layout::print_left(display, g_boot_layout.slash_2_x, g_boot_layout.baseline_y, &Satoshi_800_subset22pt7b, "/");

      aether_epaper_layout::print_left(display, g_boot_layout.right_x, g_boot_layout.baseline_y, &Satoshi_800_subset22pt7b, " Labs");
    }

    // --- High-Level Renderers ---
    inline void render_boot(uint8_t frame, bool full)
    {
      if (full)
      {
        render_paged(true, [frame]()
                     {
          display.fillScreen(GxEPD_WHITE);
          draw_boot_content(frame); });
      }
      else
      {
        int px = g_boot_layout.slash_1_x - 12, py = g_boot_layout.baseline_y - 40;
        int pw = (g_boot_layout.right_x - g_boot_layout.slash_1_x) + 24, ph = 60;

        display.setPartialWindow(px, py, pw, ph);
        display.firstPage();
        do
        {
          display.fillRect(px, py, pw, ph, GxEPD_WHITE);
          static constexpr const char *SLASHES[4] = {"//", " /", "  ", "/ "};
          const char *s = SLASHES[frame % 4];
          display.setTextColor(GxEPD_BLACK);
          if (s[0] == '/')
            aether_epaper_layout::print_left(display, g_boot_layout.slash_1_x, g_boot_layout.baseline_y, &Satoshi_800_subset22pt7b, "/");
          if (s[1] == '/')
            aether_epaper_layout::print_left(display, g_boot_layout.slash_2_x, g_boot_layout.baseline_y, &Satoshi_800_subset22pt7b, "/");
          esphome::App.feed_wdt();
        } while (display.nextPage());
      }
    }

    inline void render_normal(bool full)
    {
      render_paged(full, []()
                   {
        aether_epaper_layout::Metrics m = {g_co2, g_temp, g_rh, g_pm1, g_pm25, g_pm4, g_pm10, g_voc, g_nox};
        aether_epaper_layout::render_sketch_layout(display, m, g_use_f); });
      g_last_normal_render_ms = millis();
    }

    inline bool wifi_connected()
    {
      bool connected = false;
#ifdef USE_WIFI
      if (esphome::wifi::global_wifi_component)
        connected = esphome::wifi::global_wifi_component->is_connected();
#endif
      return connected;
    }

    inline std::string wifi_ip_string()
    {
#ifdef USE_WIFI
      if (!esphome::wifi::global_wifi_component)
        return "";
      if (!esphome::wifi::global_wifi_component->is_connected() &&
          !esphome::wifi::global_wifi_component->is_ap_active())
        return "";

      const auto addresses = esphome::wifi::global_wifi_component->get_ip_addresses();
      for (const auto &ip : addresses)
      {
        if (!ip.is_set() || !ip.is_ip4())
          continue;
        char buf[esphome::network::IP_ADDRESS_BUFFER_SIZE];
        ip.str_to(buf);
        return buf;
      }
#endif
      return "";
    }

    inline std::string info_hostname()
    {
      return esphome::App.get_name() + ".local";
    }

    inline std::string info_ip_string()
    {
      return wifi_ip_string();
    }

    inline std::string info_use_address()
    {
#ifdef USE_WIFI
      if (esphome::wifi::global_wifi_component)
      {
        const char *use_address = esphome::wifi::global_wifi_component->get_use_address();
        if (use_address != nullptr && use_address[0] != '\0')
          return use_address;
      }
#endif
      return info_hostname();
    }

    inline std::string info_dashboard_target()
    {
      const std::string ip = info_ip_string();
      if (!ip.empty())
        return "http://" + ip + "/";
      return "http://" + info_use_address() + "/";
    }

    inline const GFXfont *info_meta_font(const char *left_text, const char *right_text)
    {
      const int max_width = display.width() - 32;
      const int full_width =
          aether_epaper_layout::text_width(display, &Inter_Bold12pt7b, left_text) +
          aether_epaper_layout::text_width(display, &Inter_Bold12pt7b, right_text) + 28;
      return full_width > max_width ? &Inter_Bold9pt7b : &Inter_Bold12pt7b;
    }

    inline void draw_info_header()
    {
      display.fillScreen(GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);

      aether_epaper_layout::print_centered(display, display.width() / 2, 36,
                                            &Satoshi_800_Device_Information_Subset18pt7b,
                                            "Device Information");
      aether_epaper_layout::draw_divider(display, 154, 261, 47);
    }

    inline void draw_info_meta_row(const char *left_text, const char *right_text)
    {
      const GFXfont *meta_font = info_meta_font(left_text, right_text);
      aether_epaper_layout::print_left(display, 16, 72, meta_font, left_text);
      aether_epaper_layout::print_right(display, display.width() - 16, 72, meta_font, right_text);
    }

    inline void draw_info_cards(const char *left_label, const char *left_target,
                                const char *right_label, const char *right_target)
    {
      static constexpr int qr_box_size = 99;
      static constexpr int qr_y = 134;
      static constexpr int left_qr_x = 43;
      const int right_qr_x = display.width() - 38 - qr_box_size;
      const int left_center_x = left_qr_x + qr_box_size / 2;
      const int right_center_x = right_qr_x + qr_box_size / 2;

      draw_connected_info_bracket(true);
      draw_connected_info_bracket(false);

      aether_epaper_layout::print_centered(display, left_center_x, 123, &Inter_Bold9pt7b, left_label);
      aether_epaper_layout::print_centered(display, right_center_x, 123, &Inter_Bold9pt7b, right_label);

      draw_info_qr_card(left_qr_x, qr_y, left_target);
      draw_info_qr_card(right_qr_x, qr_y, right_target);
    }

    inline void draw_info_qr_card(int x, int y, const char *target)
    {
      draw_info_qr_card(x, y, target, 3, 6);
    }

    inline void draw_info_qr_card(int x, int y, const char *target,
                                  int scale, int padding)
    {
      static constexpr uint8_t QR_VERSION = 3;
      QRCode qr;
      uint8_t data[qrcode_getBufferSize(QR_VERSION)];
      qrcode_initText(&qr, data, QR_VERSION, 0, target);

      const int box_size = padding * 2 + qr.size * scale;

      display.fillRect(x, y, box_size, box_size, GxEPD_WHITE);
      display.drawRect(x, y, box_size, box_size, GxEPD_BLACK);

      for (uint8_t row = 0; row < qr.size; ++row)
      {
        for (uint8_t col = 0; col < qr.size; ++col)
        {
          if (!qrcode_getModule(&qr, col, row))
            continue;
          display.fillRect(x + padding + col * scale,
                           y + padding + row * scale,
                           scale, scale, GxEPD_BLACK);
        }
      }
    }

    inline void draw_connected_info_bracket(bool left_side)
    {
      const int max_x = display.width() - 1;
      aether::aether_epaper_layout::Point h_start = {28, 96};
      aether::aether_epaper_layout::Point h_end = {160, 96};
      aether::aether_epaper_layout::Point c_ctrl = {176, 96};
      aether::aether_epaper_layout::Point c_end = {182, 114};
      aether::aether_epaper_layout::Point d_end = {198, 162};

      if (!left_side)
      {
        h_start.x = max_x - h_start.x;
        h_end.x = max_x - h_end.x;
        c_ctrl.x = max_x - c_ctrl.x;
        c_end.x = max_x - c_end.x;
        d_end.x = max_x - d_end.x;
      }

      aether_epaper_layout::draw_stroked_line(display, h_start, h_end, 1);
      aether_epaper_layout::draw_stroked_quadratic(display, h_end, c_ctrl, c_end, 1);
      aether_epaper_layout::draw_stroked_line(display, c_end, d_end, 1);
    }

    inline void render_info_connected()
    {
      const std::string hostname = info_hostname();
      const std::string ip = info_ip_string();
      const std::string host_text = "Host: " + hostname;
      const std::string ip_text = "IP: " + (ip.empty() ? std::string("--") : ip);
      const std::string dashboard_target = info_dashboard_target();

      draw_info_header();
      aether_epaper_layout::draw_info_triangle(display, display.width() - 18, 13, 14, 26);
      draw_info_meta_row(host_text.c_str(), ip_text.c_str());
      draw_info_cards("Instructions", INFO_INSTRUCTIONS_URL,
                      "Local Dashboard", dashboard_target.c_str());
    }

    inline void render_info_disconnected()
    {
      const std::string setup_ap_name = esphome::App.get_name();
      aether_epaper_layout::render_disconnected_info_layout(
          display, setup_ap_name.c_str(), INFO_INSTRUCTIONS_URL,
          [](int x, int y, const char *target, int scale, int padding)
          {
            draw_info_qr_card(x, y, target, scale, padding);
          });
    }

    inline void render_info(bool full)
    {
      const bool connected = wifi_connected();
      render_paged(full, [connected]()
                    {
         if (connected) render_info_connected();
         else render_info_disconnected(); });
      g_last_info_wifi_state = connected ? 1 : 0;
    }

    inline void render_reset(bool full)
    {
      render_paged(full, []()
                   {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        aether_epaper_layout::draw_info_triangle(display, display.width() - 18, 13, 14, 26);

        aether_epaper_layout::print_centered(display, display.width() / 2, 49,
                                              &Satoshi_800_Factory_Reset_Subset22pt7b,
                                              "Factory Reset");
        aether_epaper_layout::draw_divider(display, 155, 260, 68);

        aether_epaper_layout::print_centered(display, display.width() / 2, 120, &Inter_Bold12pt7b, "Hold button (3s) to confirm");
        aether_epaper_layout::print_centered(display, display.width() / 2, 160, &Inter_Bold12pt7b, "Click once to cancel"); });
    }

    inline void redraw(bool full)
    {
      if (!s_inited)
        return;
      switch (g_display_mode)
      {
      case MODE_BOOT:
        render_boot(g_boot_frame, full);
        break;
      case MODE_NORMAL:
        render_normal(full);
        break;
      case MODE_INFO:
        render_info(full);
        break;
      case MODE_RESET:
        render_reset(full);
        break;
      }
    }

    // --- Public API ---
    inline void on_short_press()
    {
      if (g_display_mode == MODE_BOOT)
        return;
      g_display_mode = (g_display_mode == MODE_NORMAL) ? MODE_INFO : MODE_NORMAL;
      redraw(true);
    }

    inline bool on_long_press()
    {
      if (g_display_mode == MODE_BOOT)
        return false;
      if (g_display_mode == MODE_INFO)
      {
        if (g_last_info_wifi_state == 0)
          esphome::App.safe_reboot();
        return false;
      }
      if (g_display_mode == MODE_RESET)
        return true;
      g_display_mode = MODE_RESET;
      redraw(true);
      return false;
    }

    inline void tick_and_draw(float co2, float temp, float rh, float pm1, float pm25, float pm4, float pm10, float voc, float nox, bool use_f)
    {
      init_once();
      g_co2 = co2;
      g_temp = temp;
      g_rh = rh;
      g_pm1 = pm1;
      g_pm25 = pm25;
      g_pm4 = pm4;
      g_pm10 = pm10;
      g_voc = voc;
      g_nox = nox;
      g_use_f = use_f;

      unsigned long now = millis();
      if (g_display_mode == MODE_BOOT)
      {
        if (all_metrics_ready())
        {
          g_display_mode = MODE_NORMAL;
          render_normal(true);
          return;
        }

        if (g_boot_full_refreshes < 2)
        {
          render_boot(g_boot_frame, true);
          g_boot_full_refreshes++;
          g_last_boot_frame_ms = now;
          return;
        }

        if (now - g_last_boot_frame_ms >= BOOT_FRAME_MS)
        {
          g_boot_frame = (g_boot_frame + 1) % BOOT_SLASH_FRAME_COUNT;
          render_boot(g_boot_frame, false);
          g_last_boot_frame_ms = now;
        }
        return;
      }

      if (g_display_mode == MODE_NORMAL && (g_last_normal_render_ms == 0 || now - g_last_normal_render_ms >= NORMAL_REFRESH_MS))
      {
        render_normal(false);
        return;
      }

      if (g_display_mode == MODE_INFO)
      {
        int8_t current_wifi_state = wifi_connected() ? 1 : 0;
        if (current_wifi_state != g_last_info_wifi_state)
          render_info(true);
      }
    }

  } // namespace aether_epaper
} // namespace aether
