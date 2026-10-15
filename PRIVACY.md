# Privacy

Last updated: 15 October 2026 (modified version: Simply Digital DA)

Simply Digital does not use accounts, analytics, advertising, or a server
operated by the watchface developer.

## Data handled on the watch

The watchface reads the current battery level and, when the device supports
Pebble Health, daily step count and current heart rate. These values remain on
the watch and are not sent to the paired phone or any network service.

The watch persists the date, the most recently received sunrise and sunset
times, tomorrow's sunrise, the last density altitude value and when it arrived,
plus the timestamp of the last request attempt for retry control. No
Health history or coordinates are stored by the watchface. Uninstalling the
watchface removes its app-specific storage through PebbleOS.

## Location and local sun-time calculation

When sun times need refreshing, the companion JavaScript asks the paired phone
for its current latitude and longitude. The bundled SunCalc library uses those
coordinates locally on the phone to calculate sunrise and sunset. The
watchface does not store the coordinates and does not send them to the
developer.

The phone's operating-system location provider may use GPS, nearby Wi-Fi
networks, cellular services, or services operated by the platform vendor. Such
processing is controlled by the phone operating system and its location
settings, not by the watchface.

The app accepts a cached location up to six hours old, requests no high-accuracy
fix, and sets a 15-second timeout. Only calculated times and their date are sent
to the watch. The companion logs delivery status and error codes for debugging;
it does not log coordinates or Health values.

## Weather for density altitude

About every 30 minutes while the watchface is shown, the companion JavaScript
sends the phone's latitude and longitude, rounded to two decimal places (about
1 km), to the Open-Meteo forecast API (https://open-meteo.com) and receives the
current temperature, dew point and surface pressure. Density altitude is
calculated on the phone and only the result is sent to the watch. Open-Meteo's
own privacy policy applies to that request.

With a valid result stored for the current date, the watchface does not request
location again that day. If location is unavailable, it retries no sooner than
six hours later while today's sun times remain missing. A new calendar day
starts a new attempt independently of the retry interval.

If location access is denied or unavailable, the watchface continues to work
and displays dashes for the sun time and density altitude.

## Contact

For privacy questions, contact the publisher through the repository issue
tracker or the contact method in the app-store listing.
