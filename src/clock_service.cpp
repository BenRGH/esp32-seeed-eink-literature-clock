#include "clock_service.h"

#include <WiFi.h>
#include <esp_bt.h>
#include <esp_sleep.h>
#include <time.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

namespace ClockService {

namespace {

static void setOutputLevel(int8_t pin, uint8_t level)
{
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

static void holdRtcPinIfValid(int8_t pin)
{
  if (pin < 0) return;
  const gpio_num_t gpio = (gpio_num_t)pin;
  if (rtc_gpio_is_valid_gpio(gpio)) {
    gpio_hold_en(gpio);
  }
}

static void releaseRtcPinHoldIfValid(int8_t pin)
{
  if (pin < 0) return;
  const gpio_num_t gpio = (gpio_num_t)pin;
  if (rtc_gpio_is_valid_gpio(gpio)) {
    gpio_hold_dis(gpio);
  }
}

// Convert UTC date/time to Unix epoch without mktime() local-time side effects.
static time_t utcToEpoch(uint16_t year,
                         uint8_t month,
                         uint8_t day,
                         uint8_t hour,
                         uint8_t min,
                         uint8_t sec)
{
  static const uint16_t DAYS_BEFORE_MONTH[12] =
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

  const uint32_t y     = (uint32_t)year - 1970u;
  const uint32_t leaps = (y + 1u) / 4u - (y + 69u) / 100u + (y + 369u) / 400u;
  uint32_t days = y * 365u + leaps + DAYS_BEFORE_MONTH[month - 1u] + (uint32_t)day - 1u;

  const bool isLeap = (year % 4u == 0u) && (year % 100u != 0u || year % 400u == 0u);
  if (isLeap && month > 2u) days++;

  return (time_t)days * 86400L
       + (time_t)hour * 3600L
       + (time_t)min  * 60L
       + (time_t)sec;
}

} // namespace

uint16_t localMinuteNow(RV3028& rtc, const Config& cfg)
{
  int totalMins = (int)rtc.getHours() * 60 + (int)rtc.getMinutes();

  if (cfg.posixTz && cfg.posixTz[0] != '\0') {
    setenv("TZ", cfg.posixTz, 1);
    tzset();
    const time_t epoch = utcToEpoch(
      rtc.getYear(), rtc.getMonth(),   rtc.getDate(),
      rtc.getHours(), rtc.getMinutes(), rtc.getSeconds()
    );
    struct tm localTm;
    localtime_r(&epoch, &localTm);
    totalMins = localTm.tm_hour * 60 + localTm.tm_min;
  } else {
    totalMins += cfg.utcOffsetMins;
  }

  return (uint16_t)(((totalMins % 1440) + 1440) % 1440);
}

void disableRadios()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#if defined(CONFIG_BT_ENABLED)
  btStop();
#endif
}

bool syncRtcFromNtp(RV3028& rtc, const Config& cfg)
{
  if (!cfg.wifiSsid || cfg.wifiSsid[0] == '\0') {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass ? cfg.wifiPass : "");

  const uint32_t wifiDeadline = millis() + cfg.wifiConnectTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < wifiDeadline) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    disableRadios();
    return false;
  }

  configTime(0, 0, cfg.ntpServer1, cfg.ntpServer2);

  struct tm tmUtc = {};
  const uint32_t ntpDeadline = millis() + cfg.ntpSyncTimeoutMs;
  bool ok = false;
  while (millis() < ntpDeadline) {
    if (getLocalTime(&tmUtc, 500)) {
      ok = true;
      break;
    }
  }
  if (!ok || tmUtc.tm_year < 100) {
    disableRadios();
    return false;
  }

  const uint32_t tGotNtp = millis();
  const time_t baseEpoch = utcToEpoch(
    (uint16_t)(tmUtc.tm_year + 1900),
    (uint8_t)(tmUtc.tm_mon + 1),
    (uint8_t)tmUtc.tm_mday,
    (uint8_t)tmUtc.tm_hour,
    (uint8_t)tmUtc.tm_min,
    (uint8_t)tmUtc.tm_sec
  );
  const time_t adjustedEpoch = baseEpoch + (time_t)((millis() - tGotNtp + 500U) / 1000U);

  struct tm setTm;
  gmtime_r(&adjustedEpoch, &setTm);
  const bool setOk = rtc.setTime(
    setTm.tm_sec,
    setTm.tm_min,
    setTm.tm_hour,
    setTm.tm_wday,
    setTm.tm_mday,
    setTm.tm_mon + 1,
    setTm.tm_year + 1900
  );

  disableRadios();
  return setOk;
}

void clearDisplayGhosts(ClockDisplay& display, int cycles)
{
  display.init(115200, false, 20, false);
  display.setRotation(1);
  display.setFullWindow();

  auto fillAndRefresh = [&](uint16_t color) {
    display.firstPage();
    do {
      display.fillScreen(color);
    } while (display.nextPage());
  };

  for (int i = 0; i < cycles; i++) {
    fillAndRefresh(GxEPD_BLACK);
    fillAndRefresh(GxEPD_WHITE);
  }
  fillAndRefresh(GxEPD_RED);
  fillAndRefresh(GxEPD_WHITE);

  display.hibernate();
}

bool maybeRunMonthlyMaintenance(RV3028& rtc,
                                ClockDisplay& display,
                                int8_t& lastMaintenanceMonth,
                                const Config& cfg)
{
  const int day   = (int)rtc.getDate();
  const int month = (int)rtc.getMonth();
  const int hour  = (int)rtc.getHours();

  if (day != 1 || hour != cfg.monthlyClearHour) return false;
  if (lastMaintenanceMonth == month) return false;

  clearDisplayGhosts(display, cfg.monthlyClearCycles);
  syncRtcFromNtp(rtc, cfg);
  lastMaintenanceMonth = (int8_t)month;
  return true;
}

uint64_t secondsUntilNextMinute(RV3028& rtc)
{
  const uint8_t sec = rtc.getSeconds();
  if (sec >= 59u) return 1u;
  const uint64_t remain = (uint64_t)(60u - sec);
  return (remain == 0u) ? 1u : remain;
}

void prepareDisplayPinsForDeepSleep(const DisplaySleepPins& pins)
{
  // Keep panel SPI/control lines deterministic before the SoC enters deep sleep.
  setOutputLevel(pins.csPin, HIGH);   // deselect panel
  setOutputLevel(pins.dcPin, LOW);
  setOutputLevel(pins.rstPin, HIGH);
  setOutputLevel(pins.sckPin, LOW);
  setOutputLevel(pins.mosiPin, LOW);

  if (pins.busyPin >= 0) {
    pinMode(pins.busyPin, INPUT_PULLUP);
  }

  if (pins.enableRtcHold) {
    holdRtcPinIfValid(pins.csPin);
    holdRtcPinIfValid(pins.dcPin);
    holdRtcPinIfValid(pins.rstPin);
    holdRtcPinIfValid(pins.busyPin);
    gpio_deep_sleep_hold_en();
  }
}

void releaseDisplayPinsAfterWake(const DisplaySleepPins& pins)
{
  if (pins.enableRtcHold) {
    releaseRtcPinHoldIfValid(pins.csPin);
    releaseRtcPinHoldIfValid(pins.dcPin);
    releaseRtcPinHoldIfValid(pins.rstPin);
    releaseRtcPinHoldIfValid(pins.busyPin);
    gpio_deep_sleep_hold_dis();
  }
}

[[noreturn]] void enterDeepSleepTimerUs(uint64_t wakeUs)
{
  const uint64_t MIN_WAKE_US = 200000ULL;
  if (wakeUs < MIN_WAKE_US) wakeUs = MIN_WAKE_US;

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(wakeUs);
  esp_deep_sleep_start();

  while (true) {
    delay(1000);
  }
}

} // namespace ClockService
