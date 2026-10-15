// SPDX-FileCopyrightText: 2026 MrReSc
// SPDX-License-Identifier: Apache-2.0
// Modified 2026 by Dallin Sumpter: sends tomorrow's sunrise, fetches current
// weather from Open-Meteo and sends density altitude to the watch.

'use strict';

var SunTimes = require('./sun_times');
var DensityAltitude = require('./density_altitude');
var LOCATION_MAX_AGE_MS = 6 * 60 * 60 * 1000;
var WEATHER_LOCATION_MAX_AGE_MS = 30 * 60 * 1000;
var LOCATION_TIMEOUT_MS = 15000;
var WEATHER_TIMEOUT_MS = 20000;
// Shown to the nearest 100 ft: model weather is not more precise than that.
var DENSITY_ALTITUDE_ROUNDING_FT = 100;
var WEATHER_URL = 'https://api.open-meteo.com/v1/forecast';

var sunRequestInProgress = false;
var weatherRequestInProgress = false;

// Pebble.sendAppMessage must not be called again until the previous message
// is acknowledged, so sun and weather replies go through one queue.
var outbox = [];
var outboxBusy = false;

function pumpOutbox() {
  if (outboxBusy || outbox.length === 0) {
    return;
  }
  outboxBusy = true;
  var item = outbox.shift();
  var done = function() {
    outboxBusy = false;
    if (item.onComplete) {
      item.onComplete();
    }
    pumpOutbox();
  };
  Pebble.sendAppMessage(item.payload, function() {
    console.log('simply-digital: message sent');
    done();
  }, function(error) {
    console.log('simply-digital: message failed: ' + JSON.stringify(error));
    done();
  });
}

function sendToWatch(payload, onComplete) {
  outbox.push({payload: payload, onComplete: onComplete});
  pumpOutbox();
}

// Problems are reported to the watch so it can show the reason instead of
// dashes. Values must match PHONE_STATUS_* in main.c.
var STATUS_NO_PERMISSION = 1;
var STATUS_NO_LOCATION = 2;
var STATUS_NO_NETWORK = 3;

function reportStatus(status) {
  sendToWatch({'PHONE_STATUS': status});
}

function locationStatusFor(error) {
  var message = (error && error.message) ? String(error.message) : '';
  if (/permission|denied|not authori[sz]ed/i.test(message)) {
    return STATUS_NO_PERMISSION;
  }
  // Without a message, fall back to the W3C code (1 = PERMISSION_DENIED).
  if (!message && error && error.code === 1) {
    return STATUS_NO_PERMISSION;
  }
  return STATUS_NO_LOCATION;
}

function getPosition(maximumAge, onSuccess, onFailure) {
  var fail = function(error) {
    console.log('simply-digital: location unavailable (' +
                (error && error.code) + ': ' + (error && error.message) + ')');
    reportStatus(locationStatusFor(error));
    onFailure();
  };
  try {
    navigator.geolocation.getCurrentPosition(onSuccess, fail, {
      enableHighAccuracy: false,
      maximumAge: maximumAge,
      timeout: LOCATION_TIMEOUT_MS
    });
  } catch (error) {
    fail({code: 2, message: 'geolocation unavailable: ' + error.message});
  }
}

function requestSunTimes() {
  if (sunRequestInProgress) {
    return;
  }
  sunRequestInProgress = true;
  getPosition(LOCATION_MAX_AGE_MS, function(position) {
    try {
      var result = SunTimes.calculate(
          new Date(), position.coords.latitude, position.coords.longitude);
      sendToWatch({
        'SUNRISE_MINUTES': result.sunrise,
        'SUNSET_MINUTES': result.sunset,
        'SUN_DATE': result.date,
        'NEXT_SUNRISE_MINUTES': result.nextSunrise,
        'NEXT_SUN_DATE': result.nextDate
      }, function() {
        sunRequestInProgress = false;
      });
    } catch (error) {
      sunRequestInProgress = false;
      console.log('simply-digital: sun calculation failed: ' + error.message);
    }
  }, function() {
    sunRequestInProgress = false;
  });
}

function requestWeather() {
  if (weatherRequestInProgress) {
    return;
  }
  weatherRequestInProgress = true;
  var finish = function() {
    weatherRequestInProgress = false;
  };
  getPosition(WEATHER_LOCATION_MAX_AGE_MS, function(position) {
    // Two decimals (about 1 km) is plenty for model weather and keeps the
    // exact position off the network.
    var url = WEATHER_URL +
        '?latitude=' + position.coords.latitude.toFixed(2) +
        '&longitude=' + position.coords.longitude.toFixed(2) +
        '&current=temperature_2m,dew_point_2m,surface_pressure';
    var request = new XMLHttpRequest();
    request.timeout = WEATHER_TIMEOUT_MS;
    request.onload = function() {
      try {
        if (request.status !== 200) {
          throw new Error('HTTP ' + request.status);
        }
        var feet = DensityAltitude.fromOpenMeteo(
            JSON.parse(request.responseText));
        var rounded = Math.round(feet / DENSITY_ALTITUDE_ROUNDING_FT) *
            DENSITY_ALTITUDE_ROUNDING_FT;
        console.log('simply-digital: density altitude ' + feet.toFixed(0) +
                    ' ft');
        sendToWatch({'DENSITY_ALTITUDE_FT': rounded}, finish);
      } catch (error) {
        finish();
        reportStatus(STATUS_NO_NETWORK);
        console.log('simply-digital: weather failed: ' + error.message);
      }
    };
    request.onerror = function() {
      finish();
      reportStatus(STATUS_NO_NETWORK);
      console.log('simply-digital: weather request error');
    };
    request.ontimeout = function() {
      finish();
      reportStatus(STATUS_NO_NETWORK);
      console.log('simply-digital: weather request timed out');
    };
    request.open('GET', url);
    request.send();
  }, finish);
}

Pebble.addEventListener('ready', function() {
  sendToWatch({'JS_READY': 1});
});

Pebble.addEventListener('appmessage', function(event) {
  if (!event.payload) {
    return;
  }
  if (event.payload.REQUEST_SUN) {
    requestSunTimes();
  }
  if (event.payload.REQUEST_WEATHER) {
    requestWeather();
  }
});
