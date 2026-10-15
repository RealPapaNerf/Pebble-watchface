'use strict';

var assert = require('assert');
var DensityAltitude = require('../src/pkjs/density_altitude');

function near(actual, expected, tolerance, message) {
  assert.ok(Math.abs(actual - expected) <= tolerance,
            message + ': expected ' + expected + ' +/- ' + tolerance +
            ', got ' + actual.toFixed(1));
}

// ISA sea level, dry air: density altitude is 0 ft.
near(DensityAltitude.calculate(1013.25, 15), 0, 5, 'ISA sea level');

// ISA at 5,000 ft: 843.07 hPa and 5.09 C, dry air.
near(DensityAltitude.calculate(843.07, 5.094), 5000, 20, 'ISA 5000 ft');

// ISA at 10,000 ft: 696.82 hPa and -4.81 C, dry air.
near(DensityAltitude.calculate(696.82, -4.812), 10000, 30, 'ISA 10000 ft');

// Rule-of-thumb check: about +120 ft of density altitude per 1 C above ISA.
// At ISA 5,000 ft pressure, 20 C above standard should land near 7,400 ft.
near(DensityAltitude.calculate(843.07, 25.094), 7400, 150, 'ISA+20 at 5000 ft');

// Humid air is less dense, so density altitude must rise.
var dry = DensityAltitude.calculate(1000, 30);
var humid = DensityAltitude.calculate(1000, 30, 25);
assert.ok(humid > dry + 200 && humid < dry + 800,
          'humidity raises DA by a few hundred ft, got +' + (humid - dry).toFixed(0));

// Vapor pressure at 20 C dew point is about 23.4 hPa.
near(DensityAltitude.vaporPressureHpa(20), 23.37, 0.1, 'vapor pressure');

// Open-Meteo response shape.
near(DensityAltitude.fromOpenMeteo({
  current: {temperature_2m: 15, dew_point_2m: null, surface_pressure: 1013.25}
}), 0, 5, 'Open-Meteo parse');

assert.throws(function() {
  DensityAltitude.fromOpenMeteo({});
}, /missing current/);
assert.throws(function() {
  DensityAltitude.calculate(1013.25 * 29.92, 15);
}, /invalid pressure/);
assert.throws(function() {
  DensityAltitude.calculate(1013.25, NaN);
}, /invalid temperature/);

console.log('density_altitude_test: ok');
