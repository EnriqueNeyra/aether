#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Wire.h>

// Fonts used in your legacy layout
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include <Fonts/FreeSansBold9pt7b.h> // specifically needed for subscript/superscript

namespace aether {

namespace aether_display {

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
static bool s_inited = false;

// Temperature unit flag: true = °F, false = °C (default °F)
static bool g_temp_unit_f = true;

// --- Cached readings (always stored in °C internally) ---
static uint16_t g_co2 = 0;
static float g_temp = NAN, g_rh = NAN;
static float g_pm1 = NAN, g_pm25 = NAN, g_pm4 = NAN, g_pm10 = NAN;
static float g_voc = NAN;
static float g_nox = NAN;

// --- Public helpers to control unit from YAML / other components ---
inline void set_temp_unit_f(bool use_fahrenheit) {
  g_temp_unit_f = use_fahrenheit;
}
inline bool get_temp_unit_f() { return g_temp_unit_f; }

// --- Helpers ---
inline void init_once() {
  if (s_inited)
    return;

  // SPI like legacy: remap only SCK & MOSI; no MISO
  SPI.begin(EPD_SCK, -1, EPD_MOSI);

  // Init display
  display.init(115200);
  display.setRotation(1);
  pinMode(EPD_BUSY, INPUT); // safe; legacy didn't set it, but harmless

  // Clear screen fully (legacy did this prior to first draw)
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());

  s_inited = true;
}

inline void print_centered(int x, int y, const GFXfont *font,
                           const char *text) {
  display.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x - w / 2, y);
  display.print(text);
}

template <typename... Args>
inline void print_centered(int x, int y, const GFXfont *font,
                           const char *format, Args... args) {
  char buf[64];
  snprintf(buf, sizeof(buf), format, args...);
  print_centered(x, y, font, buf);
}

inline void draw_pm_reading(int center_x, int y_line, const char *label,
                            float value) {
  char valStr[16];
  snprintf(valStr, sizeof(valStr), "%.1f", value);

  int16_t x1, y1;
  uint16_t w_lab, w_val, w_ug, h;
  display.setFont(&FreeSansBold12pt7b);
  display.getTextBounds(label, 0, 0, &x1, &y1, &w_lab, &h);

  display.setFont(&FreeSansBold18pt7b);
  display.getTextBounds(valStr, 0, 0, &x1, &y1, &w_val, &h);

  display.setFont(&FreeSansBold9pt7b);
  display.getTextBounds("ug/m", 0, 0, &x1, &y1, &w_ug, &h);

  int w_unit = w_ug + 10;
  int gap1 = 12;
  int gap2 = 6;
  int total_w = w_lab + gap1 + w_val + gap2 + w_unit;

  int start_x = center_x - total_w / 2;
  int txt_y = y_line - 2;

  display.setFont(&FreeSansBold12pt7b);
  display.setCursor(start_x, txt_y);
  display.print(label);

  int val_x = start_x + w_lab + gap1;
  display.setFont(&FreeSansBold18pt7b);
  display.setCursor(val_x, txt_y);
  display.print(valStr);

  int unit_x = val_x + w_val + gap2;
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(unit_x, txt_y);
  display.print("ug/m");

  display.drawLine(unit_x + 1, txt_y, unit_x + 1, txt_y + 5, GxEPD_BLACK);
  display.drawLine(unit_x + 2, txt_y, unit_x + 2, txt_y + 5, GxEPD_BLACK);

  display.setCursor(unit_x + w_ug + 1, txt_y - 8);
  display.print("3");
}

inline void draw_temp_reading(int center_x, int y, float temp, char unitChar) {
  char temp_val_str[16];
  snprintf(temp_val_str, sizeof(temp_val_str), "%.1f", temp);
  char c_str[4];
  snprintf(c_str, sizeof(c_str), "%c", unitChar);

  int16_t x1, y1;
  uint16_t w_val, w_c, h_val;
  display.setFont(&FreeSansBold18pt7b);
  display.getTextBounds(temp_val_str, 0, 0, &x1, &y1, &w_val, &h_val);
  display.getTextBounds(c_str, 0, 0, &x1, &y1, &w_c, &h_val);

  int gap = 12; // degree symbol
  int total_w = w_val + gap + w_c;
  int start_x = center_x - total_w / 2;

  display.setCursor(start_x, y);
  display.print(temp_val_str);

  int deg_x = start_x + w_val + 5;
  display.drawCircle(deg_x + 3, y - 20, 3, GxEPD_BLACK);
  display.drawCircle(deg_x + 3, y - 20, 2, GxEPD_BLACK);

  display.setCursor(deg_x + gap - 1, y);
  display.print(c_str);
}

inline void render_legacy_layout() {
  const bool useF = g_temp_unit_f;
  const char unitChar = useF ? 'F' : 'C';

  const float displayTemp = useF ? (g_temp * 9.0f / 5.0f + 32.0f) : g_temp;

  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Lines
    display.drawLine(0, 102, 416, 102, GxEPD_BLACK);
    display.drawLine(0, 103, 416, 103, GxEPD_BLACK);

    display.drawLine(138, 0, 138, 102, GxEPD_BLACK);
    display.drawLine(139, 0, 139, 102, GxEPD_BLACK);

    display.drawLine(277, 0, 277, 102, GxEPD_BLACK);
    display.drawLine(278, 0, 278, 102, GxEPD_BLACK);

    // Horizontal lines for PMs
    display.drawLine(36, 148, 190, 148, GxEPD_BLACK);
    display.drawLine(226, 148, 380, 148, GxEPD_BLACK);
    display.drawLine(36, 194, 190, 194, GxEPD_BLACK);
    display.drawLine(226, 194, 380, 194, GxEPD_BLACK);

    display.drawLine(0, 215, 416, 215, GxEPD_BLACK);
    display.drawLine(0, 216, 416, 216, GxEPD_BLACK);

    display.setTextColor(GxEPD_BLACK);

    // Temp / RH centered at 69
    draw_temp_reading(69, 40, displayTemp, unitChar);

    print_centered(69, 90, &FreeSansBold18pt7b, "%.1f%%", g_rh);

    // CO2 centered at 208
    print_centered(208, 25, &FreeSansBold12pt7b, "CO2 (ppm)");
    print_centered(208, 80, &FreeSansBold24pt7b, "%d", g_co2);

    // VOC / NOx centered at 347 (split horizontally)
    print_centered(347, 20, &FreeSansBold12pt7b, "VOC Index");
    print_centered(347, 46, &FreeSansBold18pt7b, "%.0f", g_voc);

    // Minor horizontal line to split right block
    display.drawLine(278, 51, 416, 51, GxEPD_BLACK);

    print_centered(347, 71, &FreeSansBold12pt7b, "NOx Index");
    print_centered(347, 97, &FreeSansBold18pt7b, "%.0f", g_nox);

    // PM under-line grid
    draw_pm_reading(113, 148, "PM1", g_pm1);
    draw_pm_reading(303, 148, "PM2.5", g_pm25);
    draw_pm_reading(113, 194, "PM4", g_pm4);
    draw_pm_reading(303, 194, "PM10", g_pm10);

  } while (display.nextPage());
}

// --- Public API ---
// IMPORTANT: all temperature values passed here are in °C.
// We convert to °F for display if g_temp_unit_f == true.
inline void tick_and_draw(uint16_t co2_ppm, float temp, float rh, float pm1,
                          float pm25, float pm4, float pm10, float voc_idx,
                          float nox_idx) {
  init_once();

  // cache latest values (in °C)
  g_co2 = co2_ppm;
  g_temp = temp;
  g_rh = rh;
  g_pm1 = pm1;
  g_pm25 = pm25;
  g_pm4 = pm4;
  g_pm10 = pm10;
  g_voc = voc_idx;
  g_nox = nox_idx;

  // draw exactly like legacy loop, but with configurable unit
  render_legacy_layout();
}

} // namespace aether_display

} // namespace aether
