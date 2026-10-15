'use strict';

var assert = require('assert');
var SunTimes = require('../src/pkjs/sun_times');

var result = SunTimes.calculate(new Date(2026, 8, 7, 1, 2, 3), 47.3769, 8.5417);
assert.strictEqual(result.date, 20260907);
assert.ok(result.sunrise >= 0 && result.sunrise < 1440);
assert.ok(result.sunset >= 0 && result.sunset < 1440);
assert.throws(function() {
  SunTimes.calculate(new Date(), 91, 0);
}, /invalid coordinates/);
assert.throws(function() {
  SunTimes.calculate(new Date('invalid'), 0, 0);
}, /invalid date/);

var polar = SunTimes.calculate(new Date(2026, 11, 21, 12), 90, 0);
assert.strictEqual(polar.sunrise, SunTimes.NO_SUN_EVENT);
assert.strictEqual(polar.sunset, SunTimes.NO_SUN_EVENT);

console.log('sun_times_test: ok');
