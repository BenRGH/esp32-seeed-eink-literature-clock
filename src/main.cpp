/**
 * src/main.cpp — Literary Clock firmware
 *
 * Hardware: SEEED Studio XIAO ESP32 C3
 *           MH-ET LIVE 2.13" 3-colour e-ink display (GDEW0213Z16)
 *           RV3028 RTC
 *
 * ── WIRING ─────────────────────────────────────────────────────────────────
 *
 *   XIAO ESP32 C3         MH-ET LIVE 2.13" e-ink      RV3028 RTC
 *   ─────────────         ──────────────────────       ──────────
 *   3.3V ─────────────── VCC                           VCC (2–5V)
 *   GND  ─────────────── GND ──────────────────────── GND
 *   D10 (GPIO10) ──────── SDI / DIN   [HW SPI MOSI]
 *   D8  (GPIO8)  ──────── SCLK / CLK  [HW SPI SCK]
 *   D0  (GPIO2)  ──────── CS
 *   D1  (GPIO3)  ──────── DC
 *   D2  (GPIO4)  ──────── RST
 *   D3  (GPIO5)  ──────── BUSY
 *   D4  (GPIO6)  ──────────────────────────────────── SDA [HW I2C]
 *   D5  (GPIO7)  ──────────────────────────────────── SCL [HW I2C]
 *
 *   ⚠  GPIO0 / GPIO1 are internal USB D−/D+, not user I/O.
 *      Physical header: D0=GPIO2, D1=GPIO3, D2=GPIO4, D3=GPIO5.
 *
 * ── OPERATION ──────────────────────────────────────────────────────────────
 *  • Polls the RV3028 RTC in loop(); renders a new quote when the minute
 *    changes (time phrase in RED, rest in BLACK); delays 5 s between polls.
 *  • Display is bistable: holds image with zero power between refreshes.
 *  • Quotes with an empty time-phrase field are skipped at runtime.
 *  • Monthly maintenance runs at MONTHLY_CLEAR_HOUR UTC on the 1st:
 *    clears display ghost images, then syncs the RV3028 from NTP.
 *    WiFi is disabled at all other times.
 *
 * ── FONT ───────────────────────────────────────────────────────────────────
 *  u8g2_font_helvR10_tf  via U8g2_for_Adafruit_GFX.
 *  Helvetica Regular 10 pt, transparent, full 256-glyph range.
 *  Covers Latin Extended (accented chars: é ü ñ ç etc.) as the font's
 *  first 256 codepoints span ASCII + Latin Extended U+00A0–U+00FF.
 *  Smart-quotes and em-dashes are normalised to ASCII by the data generator.
 *
 * ── TIME ZONE ──────────────────────────────────────────────────────────────
 *  UTC_OFFSET_MINS — fixed offset (no DST).  Examples:
 *    0   UTC     60  UK BST / CET    120  CEST   -300  US EST   330  India
 *  POSIX_TZ — full POSIX string with DST rules (overrides fixed offset):
 *    "GMT0BST,M3.5.0/1,M10.5.0"      UK
 *    "CET-1CEST,M3.5.0,M10.5.0/3"    Central Europe
 *    "EST5EDT,M3.2.0,M11.1.0"         US Eastern
 *
 * ── DATA SETUP ─────────────────────────────────────────────────────────────
 *  Run  .\generate_litclock_data.ps1  from the repo root to generate
 *  src/litclock_data.h (sorted by minute).
 *  The generator normalizes and pre-trims overlong source rows, while the
 *  runtime renderer does final display-aware fitting so the time phrase stays
 *  visible: trim the end first, then the start, then balance around the phrase
 *  if the quote is still too large for the screen.
 *  Without generated data the 12-quote stub below compiles automatically.
 *
 * ── WIFI CREDENTIALS ─────────────────────────────────────────────────────
 *  Copy  src/wifi_credentials.h.template
 *  to    src/wifi_credentials.h  and fill in WIFI_SSID / WIFI_PASS.
 *  The file is git-ignored; without it NTP sync is silently skipped.
 *
 * ── FIRST-TIME RTC SETUP ──────────────────────────────────────────────
 *  Option A (quickest): add src/wifi_credentials.h and power on; NTP sync
 *    runs on the 1st of the next month at MONTHLY_CLEAR_HOUR UTC.
 *  Option B (immediate): flash any temporary RTC/NTP sketch once, then
 *    reflash this firmware.
 *
 * ── LIBRARIES (platformio.ini) ───────────────────────────────────────────
 *  zinggjm/GxEPD2                            e-ink driver
 *  adafruit/Adafruit GFX Library             graphics
 *  olikraus/U8g2_for_Adafruit_GFX            UTF-8 fonts
 *  constiko/RTC RV-3028-C7 Arduino Library   RTC
 *  WiFi (built-in to the ESP32 Arduino framework)  NTP sync
 */

#include <Arduino.h>
#include <Wire.h>
#include <esp_attr.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <RV-3028-C7.h>
#include "quote_data_provider.h"
#include "quote_lookup.h"
#include "rendering.h"
#include "clock_service.h"

// Module split:
// - quote_lookup.h: minute->quote selection and phrase-quality checks
// - rendering.*: display/layout/truncation and fatal error screen drawing
// - clock_service.*: RTC local-time conversion, NTP sync, radio power, maintenance

// ── WiFi credentials (for NTP sync) ─────────────────────────────────────────
// Copy  src/wifi_credentials.h.template  ->  src/wifi_credentials.h
#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #define WIFI_SSID ""  ///< Define in src/wifi_credentials.h
  #define WIFI_PASS ""  ///< Define in src/wifi_credentials.h
#endif

// ── Quote dataset ───────────────────────────────────────────────────────────
// quote_data_provider.h centralizes generated data vs. fallback stub wiring.

// ── Configuration ───────────────────────────────────────────────────────────
#define I2C_SDA                6       ///< D4 on XIAO ESP32 C3
#define I2C_SCL                7       ///< D5 on XIAO ESP32 C3
#define EPD_BUSY_PIN           5       ///< D3
#define UTC_OFFSET_MINS        0       ///< Fixed UTC offset in minutes (overridden by POSIX_TZ)
#define POSIX_TZ               "GMT0BST,M3.5.0/1,M10.5.0"      ///< POSIX TZ string — overrides UTC_OFFSET_MINS when set
#define MONTHLY_CLEAR_HOUR     5       ///< UTC hour (0–23) for monthly maintenance on the 1st
#define NTP_SERVER1            "pool.ntp.org"    ///< Primary NTP server
#define NTP_SERVER2            "time.nist.gov"   ///< Secondary NTP server
#define WIFI_CONNECT_TIMEOUT_MS 20000  ///< Wi-Fi connection timeout (ms)
#define NTP_SYNC_TIMEOUT_MS    10000   ///< NTP sync timeout (ms)
#define MONTHLY_CLEAR_CYCLES   4       ///< Black/white cycles used in monthly de-ghost
#define POLL_NORMAL_MS         5000    ///< Poll interval for most of the minute
#define POLL_FAST_MS           250     ///< Poll interval near minute boundary
#define POLL_FAST_START_SEC    57      ///< Use fast polling at second >= this value
#define RTC_INIT_FATAL_ATTEMPTS 30     ///< Escalate if RTC init fails continuously at boot
#define RTC_UPDATE_FATAL_STREAK 120    ///< Escalate if RTC updates fail continuously in loop
#define DEBUG_LOG              0       ///< 1 = info logs enabled, 0 = silent

// ── Display ─────────────────────────────────────────────────────────────────
// MH-ET LIVE 2.13" 3-colour, GDEW0213Z16, 104×212 px, UC8151 driver.
// initial=false on init() skips the redundant first full-refresh; the paged
// loop fills the screen directly via firstPage()/nextPage().
GxEPD2_3C<GxEPD2_213c, GxEPD2_213c::HEIGHT> display(
  GxEPD2_213c(/*CS=D0*/2, /*DC=D1*/3, /*RST=D2*/4, /*BUSY=D3*/5)
);
U8G2_FOR_ADAFRUIT_GFX u8g2f;

// ── RTC ─────────────────────────────────────────────────────────────────────
RV3028 rtc;

// ── State ─────────────────────────────────────────────────────────────────────
static uint16_t g_rtcUpdateFailStreak = 0;

static const uint32_t RETAINED_STATE_MAGIC = 0x4C434C4Bu; // 'LCLK'
RTC_DATA_ATTR static uint32_t g_retainedStateMagic = 0;
RTC_DATA_ATTR static int16_t  g_lastMinute         = -1;  ///< −1 = not yet rendered
RTC_DATA_ATTR static int8_t   g_lastMaintenanceMonth = -1; ///< 1–12, or -1 if never run
RTC_DATA_ATTR static bool     g_quoteLookupReadyRetained = false;
RTC_DATA_ATTR static uint16_t g_quoteByMinuteRetained[1440];

static const ClockService::DisplaySleepPins DISPLAY_SLEEP_PINS = {
  /*csPin=*/2,
  /*dcPin=*/3,
  /*rstPin=*/4,
  /*busyPin=*/5,
  /*sckPin=*/8,
  /*mosiPin=*/10,
  /*enableRtcHold=*/true
};

// ── Layout constants (landscape: 212 wide × 104 tall) ───────────────────────
static const int16_t MARGIN = 3;
static const int16_t MAX_X  = 212 - MARGIN;
static const int16_t ATTR_Y = 101; ///< attribution baseline (bottom of display)
static const char*   TRUNC_MARKER = "...";

static const ClockService::Config CLOCK_CFG = {
  UTC_OFFSET_MINS,
  POSIX_TZ,
  MONTHLY_CLEAR_HOUR,
  MONTHLY_CLEAR_CYCLES,
  NTP_SERVER1,
  NTP_SERVER2,
  WIFI_CONNECT_TIMEOUT_MS,
  NTP_SYNC_TIMEOUT_MS,
  WIFI_SSID,
  WIFI_PASS
};

// ── Forward declarations ─────────────────────────────────────────────────────
static bool     initQuoteLookup();
static void     renderQuote(uint16_t idx);
static void     showFatalErrorAndHalt(const char* code, const char* detail);

// Timestamped log helper. Keep disabled in production to reduce runtime overhead.
#if DEBUG_LOG
  #define TLOG(fmt, ...) do { \
    Serial.printf("[%7u ms] " fmt "\n", (unsigned)millis(), ##__VA_ARGS__); \
  } while(0)
#else
  #define TLOG(fmt, ...) do {} while(0)
#endif

// BUSY pin: LOW = display busy, HIGH = display idle (GxEPD2_213c busy_level=LOW)
#define BUSY_STATE() (digitalRead(EPD_BUSY_PIN) ? "HIGH(idle)" : "LOW(busy!)")


// ═════════════════════════════════════════════════════════════════════════════
void setup()
{
  ClockService::releaseDisplayPinsAfterWake(DISPLAY_SLEEP_PINS);

  if (g_retainedStateMagic != RETAINED_STATE_MAGIC) {
    g_retainedStateMagic = RETAINED_STATE_MAGIC;
    g_lastMinute = -1;
    g_lastMaintenanceMonth = -1;
    g_quoteLookupReadyRetained = false;
    for (uint16_t i = 0; i < 1440u; i++) g_quoteByMinuteRetained[i] = 0u;
  }

  pinMode(EPD_BUSY_PIN, INPUT);
  Serial.begin(115200);
  delay(200);

  TLOG("=== BOOT BUSY=%s ===", BUSY_STATE());

  Wire.begin(I2C_SDA, I2C_SCL);
  TLOG("rtc.begin()...");
  uint16_t rtcInitAttempts = 0;
  while (!rtc.begin()) {
    rtcInitAttempts++;
    TLOG("ERROR: rtc.begin() failed — retrying...");
    if (rtcInitAttempts >= RTC_INIT_FATAL_ATTEMPTS) {
      showFatalErrorAndHalt("RTC_INIT", "RV3028 not detected");
    }
    delay(2000);
  }
  if (!initQuoteLookup()) {
    showFatalErrorAndHalt("QUOTE_DATA", "No renderable phrases");
  }
  ClockService::disableRadios();
  TLOG("RTC OK");
}

void loop()
{
  if (!rtc.updateTime()) {
    g_rtcUpdateFailStreak++;
    TLOG("ERROR: rtc.updateTime() failed");
    if (g_rtcUpdateFailStreak >= RTC_UPDATE_FATAL_STREAK) {
      showFatalErrorAndHalt("RTC_UPDATE", "Clock read failed");
    }
    delay(2000);
    return;
  }
  g_rtcUpdateFailStreak = 0;

  if (ClockService::maybeRunMonthlyMaintenance(rtc, display, g_lastMaintenanceMonth, CLOCK_CFG)) {
    TLOG("Monthly maintenance executed");
  }
  // Maintenance can change RTC time (NTP sync), so refresh our reading.
  if (!rtc.updateTime()) {
    g_rtcUpdateFailStreak++;
    TLOG("ERROR: rtc.updateTime() failed after maintenance");
    if (g_rtcUpdateFailStreak >= RTC_UPDATE_FATAL_STREAK) {
      showFatalErrorAndHalt("RTC_UPDATE", "Clock read failed");
    }
    delay(2000);
    return;
  }
  g_rtcUpdateFailStreak = 0;

  const uint8_t secs = rtc.getSeconds();
  const uint16_t minute = ClockService::localMinuteNow(rtc, CLOCK_CFG);
  TLOG("UTC %02d:%02d:%02d  local_min=%u  last_min=%d",
       (int)rtc.getHours(), (int)rtc.getMinutes(), (int)secs,
       minute, (int)g_lastMinute);

  if (g_lastMinute != (int16_t)minute) {
    const uint16_t idx = g_quoteLookupReadyRetained
      ? g_quoteByMinuteRetained[minute]
      : QuoteLookup::findQuote(minute, QUOTES, NUM_QUOTES);
    if (!QuoteLookup::hasRenderablePhrase(QUOTES, NUM_QUOTES, idx)) {
      TLOG("Skipped quote %u for min %u: missing/blank phrase", idx, minute);
      g_lastMinute = (int16_t)minute;
    } else {
      TLOG("Rendering quote %u for min %u", idx, minute);
      const uint32_t t0 = millis();
      renderQuote(idx);
      TLOG("Render done in %u ms. BUSY=%s", (unsigned)(millis() - t0), BUSY_STATE());
      g_lastMinute = (int16_t)minute;
      display.hibernate();
      TLOG("hibernate() done. BUSY=%s", BUSY_STATE());
    }
  }

  if (!rtc.updateTime()) {
    TLOG("WARN: rtc.updateTime() failed before sleep scheduling; using 60s fallback");
  }
  const uint64_t sleepSecs = ClockService::secondsUntilNextMinute(rtc);
  const uint64_t sleepUs   = sleepSecs * 1000000ULL;

  TLOG("Entering deep sleep for %llu s", (unsigned long long)sleepSecs);
  ClockService::disableRadios();
  ClockService::prepareDisplayPinsForDeepSleep(DISPLAY_SLEEP_PINS);
  ClockService::enterDeepSleepTimerUs(sleepUs);
}

// ─────────────────────────────────────────────────────────────────────────────
/// Precompute quote lookup for all 1440 minutes.
/// This removes binary-search and phrase-validation overhead from loop().
static bool initQuoteLookup()
{
  if (g_quoteLookupReadyRetained) {
    TLOG("Quote lookup restored from retained memory");
    return true;
  }

  const uint16_t renderableCount = QuoteLookup::initLookupTable(g_quoteByMinuteRetained, QUOTES, NUM_QUOTES);
  g_quoteLookupReadyRetained = (renderableCount > 0);
  TLOG("Quote lookup initialized (1440 minutes), renderable=%u", (unsigned)renderableCount);
  return renderableCount > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
/// Render the quote at `idx`.
static void renderQuote(uint16_t idx)
{
  TLOG("  BUSY before init: %s", BUSY_STATE());
  TLOG("  Quote %u  min=%u  \"%s\"", idx, QUOTES[idx].minute, QUOTES[idx].phrase);
  TLOG("  Attr: %s", QUOTES[idx].attr);
  bool quoteFits = false;
  bool phraseFits = false;
  uint8_t attrLines = 0;
  bool attrTruncated = false;
  int16_t quoteBottomY = 0;
  renderQuoteToDisplay(
    display,
    u8g2f,
    QUOTES[idx].before,
    QUOTES[idx].phrase,
    QUOTES[idx].after,
    QUOTES[idx].attr,
    QUOTES[idx].beforeTight,
    QUOTES[idx].afterTight,
    QUOTES[idx].beforeCompact,
    QUOTES[idx].afterCompact,
    MARGIN,
    MAX_X,
    ATTR_Y,
    TRUNC_MARKER,
    &quoteFits,
    &phraseFits,
    &attrLines,
    &attrTruncated,
    &quoteBottomY
  );
  TLOG("  Runtime fit: quoteFits=%d phraseFits=%d quoteBottom=%d attrLines=%u attrTrunc=%d",
       quoteFits ? 1 : 0,
       phraseFits ? 1 : 0,
       quoteBottomY,
       (unsigned)attrLines,
       attrTruncated ? 1 : 0);
  TLOG("  page loop done. BUSY=%s", BUSY_STATE());
}

// ─────────────────────────────────────────────────────────────────────────────
/// Show an unrecoverable error on e-ink and halt. This is used only for
/// situations where core clock operation is no longer possible.
static void showFatalErrorAndHalt(const char* code, const char* detail)
{
  TLOG("FATAL: %s (%s)", code ? code : "UNKNOWN", detail ? detail : "");
  ClockService::disableRadios();
  showFatalErrorScreenAndHalt(display, u8g2f, code, detail, MARGIN, MAX_X, ATTR_Y);
}

