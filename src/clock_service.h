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

// Display-related pins that should be driven to stable levels before deep sleep.
struct DisplaySleepPins {
  int8_t csPin;
  int8_t dcPin;
  int8_t rstPin;
  int8_t busyPin;
  int8_t sckPin;
  int8_t mosiPin;
  bool   enableRtcHold;
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

// Return whole seconds until the next minute boundary, clamped to >= 1 second.
uint64_t secondsUntilNextMinute(RV3028& rtc);

// Drive display lines to deterministic low-leakage states before deep sleep.
void prepareDisplayPinsForDeepSleep(const DisplaySleepPins& pins);

// Release optional RTC GPIO hold after wake so drivers can reconfigure pins.
void releaseDisplayPinsAfterWake(const DisplaySleepPins& pins);

// Enter timer-based deep sleep. This function never returns.
[[noreturn]] void enterDeepSleepTimerUs(uint64_t wakeUs);

} // namespace ClockService
