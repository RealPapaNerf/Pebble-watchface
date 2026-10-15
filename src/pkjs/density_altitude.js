// SPDX-FileCopyrightText: 2026 Dallin Sumpter
// SPDX-License-Identifier: Apache-2.0
//
// Density altitude from station pressure, air temperature and dew point.
//
// Method (same one the US National Weather Service calculator uses):
//   1. Vapor pressure e from dew point (Tetens/Magnus form).
//   2. Virtual temperature Tv = T / (1 - (e / P) * (1 - 0.622)).
//      Humid air is lighter than dry air; Tv is the temperature dry air
//      would need to have the same density.
//   3. Air density rho = P / (Rd * Tv).
//   4. Density altitude = the ISA altitude with that density:
//      DA = (T0 / L) * (1 - (rho / rho0) ^ (L * R / (g * M - L * R)))
//         = 145442.16 ft * (1 - (rho / 1.225) ^ 0.234969)

'use strict';

var DRY_AIR_GAS_CONSTANT = 287.05;      // J / (kg K)
var EPSILON = 0.622;                    // Mw / Md
var ISA_SEA_LEVEL_DENSITY = 1.225;      // kg / m^3
var ISA_SCALE_HEIGHT_FT = 145442.16;    // T0 / L = 288.15 K / 0.0065 K/m, in ft
var ISA_DENSITY_EXPONENT = 0.234969;    // 1 / 4.25588

function isFiniteNumber(value) {
  return typeof value === 'number' && isFinite(value);
}

// Saturation vapor pressure over water at the dew point, in hPa.
function vaporPressureHpa(dewPointC) {
  return 6.1078 * Math.pow(10, (7.5 * dewPointC) / (237.3 + dewPointC));
}

// stationPressureHpa: pressure at the location (NOT sea-level / altimeter).
// temperatureC: air temperature. dewPointC: optional; omitted means dry air.
// Returns density altitude in feet.
function densityAltitudeFt(stationPressureHpa, temperatureC, dewPointC) {
  if (!isFiniteNumber(stationPressureHpa) ||
      stationPressureHpa < 300 || stationPressureHpa > 1100) {
    throw new Error('invalid pressure');
  }
  if (!isFiniteNumber(temperatureC) ||
      temperatureC < -90 || temperatureC > 60) {
    throw new Error('invalid temperature');
  }

  var vaporPressure = isFiniteNumber(dewPointC) ?
      vaporPressureHpa(Math.min(dewPointC, temperatureC)) : 0;
  var temperatureK = temperatureC + 273.15;
  var virtualTemperatureK = temperatureK /
      (1 - (vaporPressure / stationPressureHpa) * (1 - EPSILON));
  var density = (stationPressureHpa * 100) /
      (DRY_AIR_GAS_CONSTANT * virtualTemperatureK);
  return ISA_SCALE_HEIGHT_FT *
      (1 - Math.pow(density / ISA_SEA_LEVEL_DENSITY, ISA_DENSITY_EXPONENT));
}

// Reads an Open-Meteo /v1/forecast response requested with
// current=temperature_2m,dew_point_2m,surface_pressure.
function fromOpenMeteo(response) {
  var current = response && response.current;
  if (!current) {
    throw new Error('missing current conditions');
  }
  return densityAltitudeFt(current.surface_pressure, current.temperature_2m,
                           current.dew_point_2m);
}

module.exports = {
  vaporPressureHpa: vaporPressureHpa,
  calculate: densityAltitudeFt,
  fromOpenMeteo: fromOpenMeteo
};
