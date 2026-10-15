// SPDX-FileCopyrightText: 2026 MrReSc
// SPDX-License-Identifier: Apache-2.0

'use strict';

var SunTimes = require('./sun_times');
var LOCATION_MAX_AGE_MS = 6 * 60 * 60 * 1000;
var LOCATION_TIMEOUT_MS = 15000;
var requestInProgress = false;

function sendToWatch(payload, onComplete) {
  Pebble.sendAppMessage(payload, function() {
    console.log('simply-digital: message sent');
    if (onComplete) {
      onComplete();
    }
  }, function(error) {
    console.log('simply-digital: message failed: ' + JSON.stringify(error));
    if (onComplete) {
      onComplete();
    }
  });
}

function requestSunTimes() {
  if (requestInProgress) {
    return;
  }
  requestInProgress = true;
  navigator.geolocation.getCurrentPosition(function(position) {
    try {
      var result = SunTimes.calculate(
          new Date(), position.coords.latitude, position.coords.longitude);
      sendToWatch({
        'SUNRISE_MINUTES': result.sunrise,
        'SUNSET_MINUTES': result.sunset,
        'SUN_DATE': result.date
      }, function() {
        requestInProgress = false;
      });
    } catch (error) {
      requestInProgress = false;
      console.log('simply-digital: sun calculation failed: ' + error.message);
    }
  }, function(error) {
    requestInProgress = false;
    console.log('simply-digital: location unavailable (' + error.code + ')');
  }, {
    enableHighAccuracy: false,
    maximumAge: LOCATION_MAX_AGE_MS,
    timeout: LOCATION_TIMEOUT_MS
  });
}

Pebble.addEventListener('ready', function() {
  sendToWatch({'JS_READY': 1});
});

Pebble.addEventListener('appmessage', function(event) {
  if (event.payload && event.payload.REQUEST_SUN) {
    requestSunTimes();
  }
});
