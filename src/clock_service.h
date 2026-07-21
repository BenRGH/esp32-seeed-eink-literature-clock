#pragma once

#include <Arduino.h>
#include <RV-3028-C7.h>
#include <stdint.h>

#include "rendering.h"

namespace ClockService {

// Shared clock/power/network configuration used by maintenance and time logic.
struct Config {
  int         utcOffsetMins;
  const char* posixTz;
  int         monthlyClearHour;
  int         monthlyClearCycles;
  const char* ntpServer1;
  const char* ntpServer2;
  uint32_t    wifiConnectTimeoutMs;
  uint32_t    ntpSyncTimeoutMs;
  const char* wifiSsid;
  const char* wifiPass;
};

// Compute local minute-of-day (0..1439) from RTC UTC time.
// Uses POSIX TZ when provided, otherwise fixed offset.
uint16_t localMinuteNow(RV3028& rtc, const Config& cfg);

// Disable Wi-Fi and Bluetooth to minimize idle power.
void disableRadios();

// Attempt NTP sync and write UTC back to RV3028. Returns success/failure.
bool syncRtcFromNtp(RV3028& rtc, const Config& cfg);

// Full e-ink de-ghost maintenance cycle.
void clearDisplayGhosts(ClockDisplay& display, int cycles);

// Run maintenance once per month at configured UTC hour. Returns true if
// window was active and maintenance was executed this call.
bool maybeRunMonthlyMaintenance(RV3028& rtc,
                                ClockDisplay& display,
                                int8_t& lastMaintenanceMonth,
                                const Config& cfg);

} // namespace ClockService
