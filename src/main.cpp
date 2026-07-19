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
 *  src/litclock_data.h (~2 800+ quotes, sorted by minute).
 *  The generator pre-trims long leading quote text (whole words only) using
 *  a tiny "... " marker so the time phrase is more likely to remain visible.
 *  Without it the 12-quote stub below compiles automatically.
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
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <RV-3028-C7.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_bt.h>
#include <time.h>
#include <ctype.h>

// ── WiFi credentials (for NTP sync) ─────────────────────────────────────────
// Copy  src/wifi_credentials.h.template  ->  src/wifi_credentials.h
#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #define WIFI_SSID ""  ///< Define in src/wifi_credentials.h
  #define WIFI_PASS ""  ///< Define in src/wifi_credentials.h
#endif

// ── Quote dataset ───────────────────────────────────────────────────────────
// Generate src/litclock_data.h with:  .\generate_litclock_data.ps1
// The stub below keeps the project compilable without it.
#if __has_include("litclock_data.h")
  #include "litclock_data.h"
#else
  #warning "litclock_data.h not found — 12-quote stub active. Run .\\generate_litclock_data.ps1"
  struct LitQuote {
    uint16_t minute; const char* before; const char* phrase;
    const char* after; const char* attr;
  };
  // Must remain sorted by minute for the binary search to work.
  const LitQuote QUOTES[] = {
    {    0, "Church clocks began to strike ",          "midnight",      ". \"Oh, Pongo, it's tomorrow!\"",                 "The 101 Dalmatians, Dodie Smith"    },
    {   60, "The bells were just striking ",           "one o'clock",   " as she opened the front door.",                  "Middlemarch, George Eliot"          },
    {  120, "He glanced at the clock. ",               "Two o'clock",   ". He should have been asleep hours ago.",         "1984, George Orwell"                },
    {  180, "She opened her eyes. ",                   "Three o'clock", ". The house was perfectly silent.",               "Rebecca, Daphne du Maurier"         },
    {  300, "The clock on the mantelpiece pointed to ","five o'clock",  ", and the fire had burnt quite low.",             "Sherlock Holmes, Arthur Conan Doyle"},
    {  360, "At ",                                     "six o'clock",   " the alarm-clock screamed.",                     "Babbitt, Sinclair Lewis"            },
    {  480, "It was exactly ",                         "eight o'clock", " when he entered the library.",                  "The Name of the Rose, Umberto Eco"  },
    {  540, "She glanced at her bedside clock. ",      "Nine o'clock",  ". She was already late.",                        "Cogheart, Peter Bunzl"              },
    {  720, "The sun hung directly overhead at ",      "noon",          ", and the square was deserted.",                 "For Whom the Bell Tolls, Hemingway" },
    {  900, "The cathedral clock struck ",             "three",         ". He counted each stroke.",                      "Brideshead Revisited, Evelyn Waugh" },
    { 1080, "It was past ",                            "six o'clock",   " in the evening before we reached the village.", "Treasure Island, R.L. Stevenson"   },
    { 1200, "The clock showed ",                       "eight o'clock", " when she finally put down her book.",           "Jane Eyre, Charlotte Bronte"        },
  };
  const uint16_t NUM_QUOTES = 12u;
#endif

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
#define DEBUG_LOG              1       ///< 1 = info logs enabled, 0 = silent

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
static int16_t g_lastMinute           = -1; ///< −1 = not yet rendered
static int8_t  g_lastMaintenanceMonth = -1; ///< 1–12, or -1 if never run
static bool    g_quoteLookupReady     = false;
static uint16_t g_quoteByMinute[1440];

// ── Layout constants (landscape: 212 wide × 104 tall) ───────────────────────
static const int16_t MARGIN = 3;
static const int16_t MAX_X  = 212 - MARGIN;
static const int16_t ATTR_Y = 101; ///< attribution baseline (bottom of display)

// ── Forward declarations ─────────────────────────────────────────────────────
static uint16_t localMinuteNow();
static time_t   utcToEpoch(uint16_t y, uint8_t mo, uint8_t d,
                             uint8_t h,  uint8_t mi, uint8_t s);
static uint16_t findQuote(uint16_t minute);
static bool     quoteHasRenderablePhrase(uint16_t idx);
static bool     quotePhraseLooks24Hour(uint16_t idx);
static void     initQuoteLookup();
static void     renderQuote(uint16_t idx);
static void     renderSegment(const char* text, uint16_t colour,
                               int16_t& x, int16_t& y,
                               int16_t lineH, int16_t maxY);
static bool     syncNTP();
static void     clearDisplayGhosts();
static void     doMonthlyMaintenance();
static void     disableRadios();

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
  pinMode(EPD_BUSY_PIN, INPUT);
  Serial.begin(115200);
  delay(200);

  TLOG("=== BOOT BUSY=%s ===", BUSY_STATE());

  Wire.begin(I2C_SDA, I2C_SCL);
  TLOG("rtc.begin()...");
  while (!rtc.begin()) {
    TLOG("ERROR: rtc.begin() failed — retrying...");
    delay(2000);
  }
  initQuoteLookup();
  disableRadios();
  TLOG("RTC OK");
}

void loop()
{
  if (!rtc.updateTime()) {
    TLOG("ERROR: rtc.updateTime() failed");
    delay(2000);
    return;
  }

  doMonthlyMaintenance();
  // Maintenance can change RTC time (NTP sync), so refresh our reading.
  if (!rtc.updateTime()) {
    TLOG("ERROR: rtc.updateTime() failed after maintenance");
    delay(2000);
    return;
  }

  const uint8_t secs = rtc.getSeconds();
  const uint16_t minute = localMinuteNow();
  TLOG("UTC %02d:%02d:%02d  local_min=%u  last_min=%d",
       (int)rtc.getHours(), (int)rtc.getMinutes(), (int)secs,
       minute, (int)g_lastMinute);

  if (g_lastMinute != (int16_t)minute) {
    const uint16_t idx = g_quoteLookupReady ? g_quoteByMinute[minute] : findQuote(minute);
    if (!quoteHasRenderablePhrase(idx)) {
      TLOG("Skipped quote %u for min %u: missing/blank phrase", idx, minute);
      g_lastMinute = (int16_t)minute;
      delay(1000);
      return;
    }
    TLOG("Rendering quote %u for min %u", idx, minute);
    const uint32_t t0 = millis();
    renderQuote(idx);
    TLOG("Render done in %u ms. BUSY=%s", (unsigned)(millis() - t0), BUSY_STATE());
    g_lastMinute = (int16_t)minute;
    display.hibernate();
    TLOG("hibernate() done. BUSY=%s", BUSY_STATE());
  } else {
    const uint32_t pollMs = (secs >= POLL_FAST_START_SEC) ? POLL_FAST_MS : POLL_NORMAL_MS;
    TLOG("Same minute — waiting %u ms", (unsigned)pollMs);
    delay(pollMs);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
/// Returns the current local minute-of-day (0–1439).
/// Uses POSIX_TZ for full DST support when defined; otherwise applies
/// UTC_OFFSET_MINS.  The UTC→epoch conversion intentionally avoids mktime()
/// to stay independent of whatever TZ is currently set.
static uint16_t localMinuteNow()
{
  int totalMins = (int)rtc.getHours() * 60 + (int)rtc.getMinutes();

  if (POSIX_TZ[0] != '\0') {
    // Full DST support: convert UTC epoch → local via newlib's localtime_r
    setenv("TZ", POSIX_TZ, 1);
    tzset();
    const time_t epoch = utcToEpoch(
      rtc.getYear(), rtc.getMonth(),   rtc.getDate(),
      rtc.getHours(), rtc.getMinutes(), rtc.getSeconds()
    );
    struct tm localTm;
    localtime_r(&epoch, &localTm);
    totalMins = localTm.tm_hour * 60 + localTm.tm_min;
  } else {
    totalMins += UTC_OFFSET_MINS;
  }

  return (uint16_t)(((totalMins % 1440) + 1440) % 1440);
}

// ─────────────────────────────────────────────────────────────────────────────
/// Convert a UTC date/time to Unix epoch without using mktime().
/// mktime() interprets its argument as *local* time, which gives wrong results
/// when the TZ environment variable is set.  This manual conversion is always
/// TZ-independent.
static time_t utcToEpoch(uint16_t year, uint8_t month, uint8_t day,
                           uint8_t hour,  uint8_t min,   uint8_t sec)
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

// ─────────────────────────────────────────────────────────────────────────────
/// Binary-search for a random quote at `minute`.
/// Expands the search radius ±1, ±2 … up to ±60 min when no exact match
/// exists (some minutes have no entry in the dataset).
static uint16_t findQuote(uint16_t minute)
{
  for (int radius = 0; radius <= 60; radius++) {
    const int offsets[2] = { (radius == 0) ? 0 : -radius, radius };
    const int nCheck     = (radius == 0) ? 1 : 2;

    for (int oi = 0; oi < nCheck; oi++) {
      const int target = ((int)minute + offsets[oi] + 1440) % 1440;

      int lo = 0, hi = (int)NUM_QUOTES - 1;
      while (lo < hi) {
        const int mid = (lo + hi) / 2;
        ((int)QUOTES[mid].minute < target) ? (lo = mid + 1) : (hi = mid);
      }

      if ((int)QUOTES[lo].minute == target) {
        int start = lo, count = 0;
        while (start + count < (int)NUM_QUOTES &&
               (int)QUOTES[start + count].minute == target) count++;

        // First preference: phrases that look like 24-hour numeric times
        // (e.g. 00:31, 12:21, 23:59) and do not contain AM/PM markers.
        for (int tries = 0; tries < count; tries++) {
          const uint16_t pick = (uint16_t)(start + (int)(esp_random() % (uint32_t)count));
          if (quoteHasRenderablePhrase(pick) && quotePhraseLooks24Hour(pick)) return pick;
        }
        for (int i = 0; i < count; i++) {
          const uint16_t pick = (uint16_t)(start + i);
          if (quoteHasRenderablePhrase(pick) && quotePhraseLooks24Hour(pick)) return pick;
        }

        // Try random picks among this minute's candidates, but require a
        // non-empty renderable phrase so the time segment is always visible.
        for (int tries = 0; tries < count; tries++) {
          const uint16_t pick = (uint16_t)(start + (int)(esp_random() % (uint32_t)count));
          if (quoteHasRenderablePhrase(pick)) return pick;
        }
        // Fallback: deterministic scan within this minute bucket.
        for (int i = 0; i < count; i++) {
          const uint16_t pick = (uint16_t)(start + i);
          if (quoteHasRenderablePhrase(pick)) return pick;
        }
      }
    }
  }
  return 0u; // absolute fallback
}

// ─────────────────────────────────────────────────────────────────────────────
/// Return true when `QUOTES[idx].phrase` has at least one non-space character.
/// This guarantees the highlighted time part is actually visible on-screen.
static bool quoteHasRenderablePhrase(uint16_t idx)
{
  if (idx >= NUM_QUOTES) return false;
  const char* p = QUOTES[idx].phrase;
  if (!p) return false;
  while (*p) {
    if (!isspace((unsigned char)*p)) return true;
    p++;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
/// Return true when phrase contains HH:MM (or HH:MM:SS) style time and no
/// AM/PM marker. This is used to prefer 24-hour-format strings.
static bool quotePhraseLooks24Hour(uint16_t idx)
{
  if (idx >= NUM_QUOTES) return false;
  const char* p = QUOTES[idx].phrase;
  if (!p || !*p) return false;

  // Reject obvious 12-hour markers in phrase.
  for (const char* q = p; *q; ++q) {
    const char c0 = (char)tolower((unsigned char)*q);
    const char c1 = (char)tolower((unsigned char)*(q + 1));
    const char c2 = (char)tolower((unsigned char)*(q + 2));
    const char c3 = (char)tolower((unsigned char)*(q + 3));
    const char c4 = (char)tolower((unsigned char)*(q + 4));
    if ((c0 == 'a' || c0 == 'p') &&
        ((c1 == 'm') ||
         (c1 == '.' && c2 == 'm') ||
         (c1 == '.' && c2 == 'm' && c3 == '.') ||
         (c1 == ' ' && c2 == 'm') ||
         (c1 == ' ' && c2 == '.' && c3 == 'm') ||
         (c1 == ' ' && c2 == '.' && c3 == 'm' && c4 == '.'))) {
      return false;
    }
  }

  // Look for HH:MM or HH:MM:SS pattern.
  for (const char* q = p; q[4] != '\0'; ++q) {
    if (!isdigit((unsigned char)q[0]) || !isdigit((unsigned char)q[1]) ||
        q[2] != ':' ||
        !isdigit((unsigned char)q[3]) || !isdigit((unsigned char)q[4])) {
      continue;
    }

    const int hh = (q[0] - '0') * 10 + (q[1] - '0');
    const int mm = (q[3] - '0') * 10 + (q[4] - '0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) continue;

    // Accept HH:MM; also accept HH:MM:SS when present and valid.
    if (q[5] == '\0' || !isdigit((unsigned char)q[5])) return true;
    if (q[5] == ':' && isdigit((unsigned char)q[6]) && isdigit((unsigned char)q[7])) {
      const int ss = (q[6] - '0') * 10 + (q[7] - '0');
      if (ss >= 0 && ss <= 59) return true;
    }
  }

  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
/// Precompute quote lookup for all 1440 minutes.
/// This removes binary-search and phrase-validation overhead from loop().
static void initQuoteLookup()
{
  for (int m = 0; m < 1440; m++) {
    g_quoteByMinute[m] = findQuote((uint16_t)m);
  }
  g_quoteLookupReady = true;
  TLOG("Quote lookup initialized (1440 minutes)");
}

// ─────────────────────────────────────────────────────────────────────────────
/// Render the quote at `idx`.
static void renderQuote(uint16_t idx)
{
  TLOG("  BUSY before init: %s", BUSY_STATE());
  TLOG("  display.init()...");
  display.init(115200, false, 20, false); // 20 ms RST pulse — required after hibernate()
  TLOG("  display.init() done. BUSY=%s", BUSY_STATE());

  display.setRotation(1); // landscape: 212 wide × 104 tall
  u8g2f.begin(display);
  u8g2f.setFont(u8g2_font_helvR10_tf);
  u8g2f.setBackgroundColor(GxEPD_WHITE);

  const int16_t ASCENT  = (int16_t)u8g2f.getFontAscent();
  const int16_t DESCENT = (int16_t)u8g2f.getFontDescent();
  const int16_t LINE_H  = ASCENT - DESCENT + 2;
  const int16_t START_Y = MARGIN + ASCENT;
  const int16_t MAX_Y   = ATTR_Y - LINE_H - 2;

  TLOG("  Quote %u  min=%u  \"%s\"", idx, QUOTES[idx].minute, QUOTES[idx].phrase);
  TLOG("  Attr: %s", QUOTES[idx].attr);

  display.setFullWindow();
  TLOG("  firstPage()...");
  display.firstPage();
  TLOG("  firstPage() returned. BUSY=%s", BUSY_STATE());
  int pageNum = 0;
  do {
    TLOG("  page %d: drawing...", pageNum++);
    display.fillScreen(GxEPD_WHITE);

    int16_t x = MARGIN, y = START_Y;
    renderSegment(QUOTES[idx].before, GxEPD_BLACK, x, y, LINE_H, MAX_Y);
    renderSegment(QUOTES[idx].phrase, GxEPD_RED,   x, y, LINE_H, MAX_Y);
    renderSegment(QUOTES[idx].after,  GxEPD_BLACK, x, y, LINE_H, MAX_Y);

    char attrBuf[50];
    snprintf(attrBuf, sizeof(attrBuf), "-- %.46s", QUOTES[idx].attr);
    u8g2f.setForegroundColor(GxEPD_BLACK);
    u8g2f.drawUTF8(MARGIN, ATTR_Y, attrBuf);

    TLOG("  page %d: calling nextPage()...", pageNum - 1);
  } while (display.nextPage());
  TLOG("  page loop done. BUSY=%s", BUSY_STATE());
}

// ─────────────────────────────────────────────────────────────────────────────
/// Force Wi-Fi and Bluetooth off to minimize idle power draw.
static void disableRadios()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#if defined(CONFIG_BT_ENABLED)
  btStop();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
/// Fetch UTC time from NTP and write it to the RV3028.
/// Returns true on success. Wi-Fi/Bluetooth are disabled before return.
static bool syncNTP()
{
  if (WIFI_SSID[0] == '\0') {
    TLOG("NTP skipped: src/wifi_credentials.h missing");
    return false;
  }

  TLOG("NTP sync: connecting Wi-Fi SSID '%s'", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t wifiDeadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < wifiDeadline) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    TLOG("NTP sync: Wi-Fi connect timeout");
    disableRadios();
    return false;
  }

  TLOG("NTP sync: connected, IP=%s", WiFi.localIP().toString().c_str());
  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);

  struct tm tmUtc = {};
  const uint32_t ntpDeadline = millis() + NTP_SYNC_TIMEOUT_MS;
  bool ok = false;
  while (millis() < ntpDeadline) {
    if (getLocalTime(&tmUtc, 500)) {
      ok = true;
      break;
    }
  }
  if (!ok || tmUtc.tm_year < 100) {
    TLOG("NTP sync: failed");
    disableRadios();
    return false;
  }

  // Convert to epoch, then compensate for elapsed processing delay before
  // writing to RTC (coarse but effective at second-level resolution).
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

  if (!setOk) {
    TLOG("NTP sync: rtc.setTime() failed");
    return false;
  }

  TLOG("NTP sync: RTC updated to %04d-%02d-%02d %02d:%02d:%02d UTC",
       setTm.tm_year + 1900, setTm.tm_mon + 1, setTm.tm_mday,
       setTm.tm_hour, setTm.tm_min, setTm.tm_sec);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
/// Run a full display de-ghost sequence.
static void clearDisplayGhosts()
{
  TLOG("Monthly clear: begin (%d BW cycles + red/white)", MONTHLY_CLEAR_CYCLES);
  display.init(115200, false, 20, false);
  display.setRotation(1);
  display.setFullWindow();

  auto fillAndRefresh = [&](uint16_t color, const char* label) {
    TLOG("  clear: %s", label);
    display.firstPage();
    do {
      display.fillScreen(color);
    } while (display.nextPage());
  };

  for (int i = 0; i < MONTHLY_CLEAR_CYCLES; i++) {
    fillAndRefresh(GxEPD_BLACK, "BLACK");
    fillAndRefresh(GxEPD_WHITE, "WHITE");
  }
  fillAndRefresh(GxEPD_RED, "RED");
  fillAndRefresh(GxEPD_WHITE, "WHITE final");

  display.hibernate();
  TLOG("Monthly clear: done");
}

// ─────────────────────────────────────────────────────────────────────────────
/// On the 1st day of each month at MONTHLY_CLEAR_HOUR UTC, run display
/// maintenance and attempt NTP sync once for that month.
static void doMonthlyMaintenance()
{
  const int day   = (int)rtc.getDate();
  const int month = (int)rtc.getMonth();
  const int hour  = (int)rtc.getHours();

  if (day != 1 || hour != MONTHLY_CLEAR_HOUR) return;
  if (g_lastMaintenanceMonth == month) return;

  TLOG("Monthly maintenance window reached (%04d-%02d-%02d %02d:%02d:%02d UTC)",
       (int)rtc.getYear(), (int)rtc.getMonth(), (int)rtc.getDate(),
       (int)rtc.getHours(), (int)rtc.getMinutes(), (int)rtc.getSeconds());

  clearDisplayGhosts();
  syncNTP();
  g_lastMaintenanceMonth = (int8_t)month;
}

// ─────────────────────────────────────────────────────────────────────────────
/// Word-wrap `text` in `colour`, advancing the shared cursor (x, y).
/// UTF-8 codepoints are never split at word or buffer boundaries.
static void renderSegment(const char* text, uint16_t colour,
                           int16_t& x, int16_t& y,
                           int16_t lineH, int16_t maxY)
{
  if (!text || !*text) return;
  u8g2f.setForegroundColor(colour);

  char           wordBuf[256];
  int            wLen = 0;
  const uint8_t* p    = (const uint8_t*)text;

  auto flushWord = [&]() {
    if (!wLen) return;
    wordBuf[wLen] = '\0';
    const int16_t ww = (int16_t)u8g2f.getUTF8Width(wordBuf);
    if (x + ww > MAX_X && x > MARGIN) { x = MARGIN; y += lineH; }
    if (y <= maxY) { u8g2f.setForegroundColor(colour); u8g2f.drawUTF8(x, y, wordBuf); }
    x += ww; wLen = 0;
  };

  while (*p) {
    if (*p == ' ') {
      flushWord(); x += (int16_t)u8g2f.getUTF8Width(" "); p++;
    } else if (*p == '\n' || *p == '\r') {
      flushWord(); x = MARGIN; y += lineH; p++;
    } else {
      const int seq = (*p < 0x80u) ? 1 : (*p < 0xE0u) ? 2 : (*p < 0xF0u) ? 3 : 4;
      if (wLen + seq < (int)sizeof(wordBuf) - 1)
        for (int i = 0; i < seq; i++) wordBuf[wLen++] = (char)*p++;
      else
        p += seq; // skip if word buffer would overflow (extremely rare)
    }
  }
  flushWord();
}

