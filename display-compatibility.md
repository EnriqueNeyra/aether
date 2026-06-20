# E-Paper Display Compatibility

The Aether project is designed around the **GDEY037T03** (3.7" 416x240) black-and-white e-paper display, which uses the **UC8253** driver IC. 

However, both the hardware (PCB) and software (firmware) are highly adaptable to other e-paper displays.

## Hardware Compatibility (The 24-Pin Standard)

The Aether PCB implements the **standard 24-pin FPC (0.5mm pitch) e-paper interface**. This is the industry standard for small-to-medium raw e-ink panels from manufacturers like Good Display and Waveshare.

Because of this standard, the custom PCB is **hardware-compatible** with a wide variety of other e-paper driver ICs. The external boost circuit on the board (MOSFET, inductor, and schottky diode) that generates the panel's internal driving voltages is universally shared across these controller families.

**Note: While the hardware standard and firmware library theoretically support the ICs listed below, they have not yet been explicitly tested or confirmed to work with the Aether device.**

Other common e-paper driver ICs that use this exact same 24-pin hardware footprint include:

*   **Solomon Systech ICs:**
    *   **SSD1680** (Very common on 2.13", 2.66", and 2.9" panels)
    *   **SSD1677**
    *   **SSD1619**
*   **UltraChip ICs** (Same manufacturer as the UC8253):
    *   **UC8151D / UC8151C** (Common on 1.54" and 2.9" panels)
    *   **UC8159** (Used for 7-color ACeP displays)
    *   **UC8179** (Common on 4.2" panels)
*   **Ilitek ICs:**
    *   **IL0373 / IL0398** (Frequently used for 3-color Black/White/Red panels and some larger B/W screens)

## Software Compatibility (GxEPD2)

While the physical PCB footprint is compatible with all of the ICs listed above, **driver ICs are not natively software-compatible with one another**. Every IC has its own specific command set, memory mapping, and timing waveforms required to drive the e-ink microcapsules.

Fortunately, the Aether firmware uses the `GxEPD2` library, which is the most comprehensive e-paper driver library available for the ESP32. **`GxEPD2` supports all of the ICs listed above.**

### How to Switch Displays in Firmware

To support a display with a different IC, you do not need to rewrite any graphics logic. You only need to update the display class instantiation in the firmware.

Open `firmware/components/aether_epaper/aether_epaper.h`. Currently, it initializes the UC8253:

```cpp
// GDEY037T03 (416x240) using UC8253
static GxEPD2_BW<GxEPD2_370_GDEY037T03, GxEPD2_370_GDEY037T03::HEIGHT>
    display(GxEPD2_370_GDEY037T03(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
```

If you were to swap the display to a 2.9" panel using the **SSD1680** IC (e.g., part number GDEY029T94), you would look up its class in the `GxEPD2` documentation and update those lines:

```cpp
// GDEY029T94 (296x128) using SSD1680
static GxEPD2_BW<GxEPD2_290_GDEY029T94, GxEPD2_290_GDEY029T94::HEIGHT>
    display(GxEPD2_290_GDEY029T94(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
```

Because `GxEPD2` extends the standard Adafruit GFX library, all existing drawing commands (`display.print`, `display.drawRect`, etc.) will continue to work perfectly. 

*(Note: Depending on the resolution of the new display, you may need to adjust the UI layout coordinates in `aether_epaper_layout.h` to fit the new screen dimensions).*
