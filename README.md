# Simply Digital

A monochrome retro-digital watchface for Pebble Time 2 (Emery, 200 × 228) with
a large seven-segment clock, compact status rows and a rounded LCD-style frame.

This watchface was developed with the assistance of coding AI.

- Automatic 12/24-hour time and localized weekday.
- Date with two-digit year and battery percentage with graphical charge level.
- Pebble Health steps and current heart rate when available.
- Sunrise and sunset calculated locally on the paired phone with SunCalc 1.9.0.
- Fixed information fields show dashes when a value is unavailable.

The clock updates once per minute and has no configuration page. Its 72 px
Digital Numbers digits use compact spacing and a one-pixel horizontal overdraw
for stronger segments. Status values use Pebble's Gothic 24 Bold system font;
long values fall back to Gothic 18 Bold so they remain fully visible. Crisp
5 × 7 pixel labels, a bold pixel weekday, and a reversed `24H`/`AM`/`PM`
indicator give the interface its 1980s instrument-LCD style.
The three status rows use the same 32 px height and vertically balanced
label/value groups.

## Build and install

Requires the Pebble SDK and CLI; verified with SDK 4.33.1 and CLI 5.0.40. No npm
dependencies are needed. The optional agent skills are not required for the
build.

```sh
pebble build
pebble install --emulator emery
pebble install --phone <IP_ADDRESS>
```

Output: `build/simply-digital.pbw`. Other Pebble models are not supported.

## Sun times and privacy

The phone calculates sun times locally, normally once per day. Today's stored
result prevents further requests. Missing results are retried at six-hour clock
boundaries after at least six hours; a new day starts a fresh attempt.

The app makes no web requests and does not store or transmit coordinates to a
web service. The phone's location provider may use GPS, Wi-Fi or cellular
services. Health data stays on the watch. See [PRIVACY.md](PRIVACY.md).

Sun times use the phone's local time zone, including daylight saving; phone and
watch should use the same time zone. A location change may not be reflected
until the next day. Terrain, observer height and current weather are not
modeled. If either sun event is unavailable, both fields show dashes.

## Publication

Use the PBW from `build/`, screenshots and icons in `store-assets/`, and the
text in [STORE_LISTING.md](STORE_LISTING.md). Supply public privacy and support
links in the store entry. The build includes the complete licenses and notices
in the PBW.

## License

Original work: Copyright 2026 MrReSc, [Apache-2.0](LICENSE). Digital Numbers:
SIL Open Font License 1.1. SunCalc 1.9.0: BSD-2-Clause, bundled unchanged. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution and licenses.

Independent watchface; not an official Casio, Pebble or Core Devices app.
