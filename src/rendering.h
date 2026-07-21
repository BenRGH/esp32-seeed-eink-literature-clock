#pragma once

#include <Arduino.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>

// Display type used by this project (MH-ET LIVE 2.13" 3-color panel).
using ClockDisplay = GxEPD2_3C<GxEPD2_213c, GxEPD2_213c::HEIGHT>;

// Render one quote to the e-ink display using runtime layout fitting.
// The renderer preserves time visibility by trimming end first, then start,
// and finally balancing around the phrase when needed.
void renderQuoteToDisplay(ClockDisplay& display,
                          U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                          const char* before,
                          const char* phrase,
                          const char* after,
                          const char* attr,
                          const char* beforeTight,
                          const char* afterTight,
                          const char* beforeCompact,
                          const char* afterCompact,
                          int16_t margin,
                          int16_t maxX,
                          int16_t attrY,
                          const char* truncMarker,
                          bool* outQuoteFits = nullptr,
                          bool* outPhraseFits = nullptr,
                          uint8_t* outAttrLines = nullptr,
                          bool* outAttrTruncated = nullptr,
                          int16_t* outQuoteBottomY = nullptr);

// Show an unrecoverable on-screen error and halt forever.
// This should be called only for fatal conditions where core clock operation
// is no longer possible.
[[noreturn]] void showFatalErrorScreenAndHalt(ClockDisplay& display,
                                              U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                                              const char* code,
                                              const char* detail,
                                              int16_t margin,
                                              int16_t maxX,
                                              int16_t attrY);
