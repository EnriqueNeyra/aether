# Display fonts

The `*pt7b.h` headers are Adafruit GFX bitmap fonts generated from the typefaces below. They are what the firmware compiles in. Third-party fonts keep their own licenses, separate from the firmware's GPLv3 (see `firmware/LICENSE`).

| Headers | Typeface | License |
| ------- | -------- | ------- |
| `Inter_*.h` | [Inter](https://github.com/rsms/inter), © The Inter Project Authors | SIL Open Font License 1.1, see [Inter-OFL.txt](Inter-OFL.txt) |
| `Satoshi_800_Logo_Subset22pt7b.h` | Satoshi, © Indian Type Foundry (ITF) | [Fontshare terms](https://www.fontshare.com/terms) |

The Inter source files (`Inter-*.ttf`) are kept here so the headers can be regenerated.

Satoshi is a trademark of the Indian Type Foundry. Only the glyphs of the "Syntropy // Labs" boot wordmark are included, as a bitmap subset. The Satoshi font file itself is not redistributed in this repo. To regenerate that header, download Satoshi from [Fontshare](https://www.fontshare.com/fonts/satoshi) under its terms.
