// SPDX-FileCopyrightText: 2026 MrReSc
// SPDX-License-Identifier: Apache-2.0
// Modified 2026 by Dallin Sumpter: also returns tomorrow's sunrise.

'use strict';

var SunCalc = require('./vendor/suncalc');
var NO_SUN_EVENT = -1;

function isFiniteNumber(value) {
  return typeof value === 'number' && isFinite(value);
}

function isValidCoordinate(latitude, longitude) {
  return isFiniteNumber(latitude) && latitude >= -90 && latitude <= 90 &&
      isFiniteNumber(longitude) && longitude >= -180 && longitude <= 180;
}

function isValidDate(value) {
  return value instanceof Date && isFinite(value.getTime());
}

function dateCode(date) {
  return date.getFullYear() * 10000 +
      (date.getMonth() + 1) * 100 + date.getDate();
}

function minutesOfDay(date) {
  return date.getHours() * 60 + date.getMinutes();
}

function calculateSunTimes(date, latitude, longitude) {
  if (!isValidDate(date)) {
    throw new Error('invalid date');
  }
  if (!isValidCoordinate(latitude, longitude)) {
    throw new Error('invalid coordinates');
  }

  var calculationDate = new Date(date.getTime());
  calculationDate.setHours(12, 0, 0, 0);
  var times = SunCalc.getTimes(calculationDate, latitude, longitude);
  var hasSunEvents = isValidDate(times.sunrise) && isValidDate(times.sunset);

  // Tomorrow's sunrise, so the watch can show it after today's sunset.
  var nextDay = new Date(date.getFullYear(), date.getMonth(),
                         date.getDate() + 1, 12, 0, 0, 0);
  var nextTimes = SunCalc.getTimes(nextDay, latitude, longitude);
  return {
    date: dateCode(date),
    sunrise: hasSunEvents ? minutesOfDay(times.sunrise) : NO_SUN_EVENT,
    sunset: hasSunEvents ? minutesOfDay(times.sunset) : NO_SUN_EVENT,
    nextDate: dateCode(nextDay),
    nextSunrise: isValidDate(nextTimes.sunrise) ?
        minutesOfDay(nextTimes.sunrise) : NO_SUN_EVENT
  };
}

module.exports = {
  NO_SUN_EVENT: NO_SUN_EVENT,
  calculate: calculateSunTimes
};
