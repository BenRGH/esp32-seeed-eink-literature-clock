#include "rendering.h"

#include <ctype.h>

namespace {

constexpr const uint8_t* kQuoteFontRegular = u8g2_font_helvR10_tf;
constexpr const uint8_t* kQuoteFontPhrase  = u8g2_font_helvB10_tf;

struct QuoteLayoutMetrics {
  int16_t bottomY;
  bool    quoteFits;
  bool    phraseFits;
};

struct AttrLayout {
  String  lines[2];
  uint8_t lineCount;
  bool    truncated;
};

static bool isSpaceByte(char c)
{
  return isspace((unsigned char)c) != 0;
}

static String trimLeadingSpacesCopy(const String& text)
{
  int start = 0;
  while (start < (int)text.length() && isSpaceByte(text[start])) start++;
  return text.substring(start);
}

static String trimTrailingSpacesCopy(const String& text)
{
  int end = (int)text.length();
  while (end > 0 && isSpaceByte(text[end - 1])) end--;
  return text.substring(0, end);
}

static void removeLastUtf8Codepoint(String& text)
{
  int end = (int)text.length();
  if (end <= 0) return;
  do {
    end--;
  } while (end > 0 && (((uint8_t)text[end] & 0xC0u) == 0x80u));
  text.remove(end);
}

static String fitTextToWidth(U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                             String text,
                             int16_t maxWidth,
                             const char* truncMarker)
{
  text = trimTrailingSpacesCopy(text);
  if (text.isEmpty()) return text;
  if ((int16_t)u8g2f.getUTF8Width(text.c_str()) <= maxWidth) return text;

  while (!text.isEmpty() &&
         (int16_t)u8g2f.getUTF8Width((text + truncMarker).c_str()) > maxWidth) {
    removeLastUtf8Codepoint(text);
    text = trimTrailingSpacesCopy(text);
  }

  if (text.isEmpty()) return String(truncMarker);
  return text + truncMarker;
}

static String trimAfterOneWord(const String& after, const char* truncMarker)
{
  String trimmed = trimTrailingSpacesCopy(after);
  if (trimmed.isEmpty()) return String("");

  int pos = (int)trimmed.length();
  while (pos > 0 && !isSpaceByte(trimmed[pos - 1])) pos--;
  String kept = trimTrailingSpacesCopy(trimmed.substring(0, pos));
  if (kept.isEmpty()) return String("");
  if (kept.endsWith(truncMarker)) return kept;
  return kept + truncMarker;
}

static String trimBeforeOneWord(const String& before, const char* truncMarker)
{
  String trimmed = trimLeadingSpacesCopy(before);
  if (trimmed.isEmpty()) return String("");

  int pos = 0;
  while (pos < (int)trimmed.length() && !isSpaceByte(trimmed[pos])) pos++;
  while (pos < (int)trimmed.length() && isSpaceByte(trimmed[pos])) pos++;

  String kept = trimLeadingSpacesCopy(trimmed.substring(pos));
  if (kept.isEmpty()) return String("");
  if (kept.startsWith(truncMarker)) return kept;
  return String(truncMarker) + kept;
}

static void walkWrappedSegment(U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                               const char* text,
                               bool isPhrase,
                               const uint8_t* font,
                               uint16_t colour,
                               bool draw,
                               int16_t margin,
                               int16_t maxX,
                               int16_t& x,
                               int16_t& y,
                               int16_t lineH,
                               int16_t maxY,
                               int16_t& bottomY,
                               bool& overflow,
                               bool& phraseSeen,
                               bool& phraseFits)
{
  if (!text || !*text) return;
  u8g2f.setFont(font ? font : kQuoteFontRegular);
  if (draw) u8g2f.setForegroundColor(colour);

  char           wordBuf[256];
  int            wLen = 0;
  const uint8_t* p    = (const uint8_t*)text;

  auto flushWord = [&]() {
    if (!wLen) return;
    wordBuf[wLen] = '\0';
    const int16_t ww = (int16_t)u8g2f.getUTF8Width(wordBuf);
    if (x + ww > maxX && x > margin) {
      x = margin;
      y += lineH;
    }
    if (y > maxY) {
      overflow = true;
      if (isPhrase) phraseFits = false;
    } else {
      if (draw) {
        u8g2f.setForegroundColor(colour);
        u8g2f.drawUTF8(x, y, wordBuf);
      }
      if (y > bottomY) bottomY = y;
    }
    if (isPhrase) phraseSeen = true;
    x += ww;
    wLen = 0;
  };

  while (*p) {
    if (*p == ' ') {
      flushWord();
      if (x > margin) {
        const int16_t spaceW = (int16_t)u8g2f.getUTF8Width(" ");
        if (x + spaceW > maxX) {
          x = margin;
          y += lineH;
        } else {
          x += spaceW;
        }
        if (y > maxY) overflow = true;
      }
      p++;
    } else if (*p == '\n' || *p == '\r') {
      flushWord();
      x = margin;
      y += lineH;
      if (y > maxY) overflow = true;
      p++;
    } else {
      const int seq = (*p < 0x80u) ? 1 : (*p < 0xE0u) ? 2 : (*p < 0xF0u) ? 3 : 4;
      if (wLen + seq < (int)sizeof(wordBuf) - 1) {
        for (int i = 0; i < seq; i++) wordBuf[wLen++] = (char)*p++;
      } else {
        p += seq;
      }
    }
  }
  flushWord();
}

static QuoteLayoutMetrics measureQuoteLayout(U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                                             const String& before,
                                             const String& phrase,
                                             const String& after,
                                             int16_t margin,
                                             int16_t maxX,
                                             int16_t startY,
                                             int16_t lineH,
                                             int16_t maxY)
{
  int16_t x = margin;
  int16_t y = startY;
  int16_t bottomY = startY;
  bool overflow = false;
  bool phraseSeen = false;
  bool phraseFits = true;

  walkWrappedSegment(u8g2f, before.c_str(), false, kQuoteFontRegular, GxEPD_BLACK, false,
                     margin, maxX, x, y, lineH, maxY,
                     bottomY, overflow, phraseSeen, phraseFits);
  walkWrappedSegment(u8g2f, phrase.c_str(), true, kQuoteFontPhrase, GxEPD_RED, false,
                     margin, maxX, x, y, lineH, maxY,
                     bottomY, overflow, phraseSeen, phraseFits);
  walkWrappedSegment(u8g2f, after.c_str(), false, kQuoteFontRegular, GxEPD_BLACK, false,
                     margin, maxX, x, y, lineH, maxY,
                     bottomY, overflow, phraseSeen, phraseFits);

  QuoteLayoutMetrics metrics = {
    bottomY,
    !overflow,
    phraseSeen && phraseFits
  };
  return metrics;
}

static void fitQuoteToDisplay(U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                              String& before,
                              const String& phrase,
                              String& after,
                              int16_t margin,
                              int16_t maxX,
                              int16_t startY,
                              int16_t lineH,
                              int16_t maxY,
                              const char* truncMarker,
                              QuoteLayoutMetrics& metrics)
{
  metrics = measureQuoteLayout(u8g2f, before, phrase, after, margin, maxX, startY, lineH, maxY);

  while (!metrics.quoteFits && !after.isEmpty()) {
    const String nextAfter = trimAfterOneWord(after, truncMarker);
    if (nextAfter == after) break;
    after = nextAfter;
    metrics = measureQuoteLayout(u8g2f, before, phrase, after, margin, maxX, startY, lineH, maxY);
  }

  while ((!metrics.phraseFits || !metrics.quoteFits) && !before.isEmpty()) {
    const String nextBefore = trimBeforeOneWord(before, truncMarker);
    if (nextBefore == before) break;
    before = nextBefore;
    metrics = measureQuoteLayout(u8g2f, before, phrase, after, margin, maxX, startY, lineH, maxY);
  }

  bool trimBeforeNext = before.length() > after.length();
  while (!metrics.quoteFits && (!before.isEmpty() || !after.isEmpty())) {
    bool changed = false;
    if (trimBeforeNext && !before.isEmpty()) {
      const String nextBefore = trimBeforeOneWord(before, truncMarker);
      if (nextBefore != before) {
        before = nextBefore;
        changed = true;
      }
    } else if (!after.isEmpty()) {
      const String nextAfter = trimAfterOneWord(after, truncMarker);
      if (nextAfter != after) {
        after = nextAfter;
        changed = true;
      }
    } else if (!before.isEmpty()) {
      const String nextBefore = trimBeforeOneWord(before, truncMarker);
      if (nextBefore != before) {
        before = nextBefore;
        changed = true;
      }
    }

    if (!changed) break;
    trimBeforeNext = !trimBeforeNext;
    metrics = measureQuoteLayout(u8g2f, before, phrase, after, margin, maxX, startY, lineH, maxY);
  }
}

static int findAttrBreak(U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                         const String& text,
                         int start,
                         int16_t maxWidth)
{
  int idx = start;
  while (idx < (int)text.length() && isSpaceByte(text[idx])) idx++;
  start = idx;

  int best = start;
  while (idx < (int)text.length()) {
    while (idx < (int)text.length() && !isSpaceByte(text[idx])) idx++;
    while (idx < (int)text.length() && isSpaceByte(text[idx])) idx++;

    const String candidate = trimTrailingSpacesCopy(text.substring(start, idx));
    if (candidate.isEmpty()) {
      best = idx;
      continue;
    }
    if ((int16_t)u8g2f.getUTF8Width(candidate.c_str()) <= maxWidth) {
      best = idx;
    } else {
      break;
    }
  }
  return best;
}

static AttrLayout layoutAttribution(U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                                    const String& text,
                                    uint8_t maxLines,
                                    int16_t maxWidth,
                                    const char* truncMarker)
{
  AttrLayout out = { { String(), String() }, 0u, false };
  int start = 0;
  while (start < (int)text.length() && isSpaceByte(text[start])) start++;

  for (uint8_t line = 0; line < maxLines && start < (int)text.length(); line++) {
    int next = findAttrBreak(u8g2f, text, start, maxWidth);
    if (next <= start) {
      out.lines[out.lineCount++] = fitTextToWidth(u8g2f, text.substring(start), maxWidth, truncMarker);
      out.truncated = true;
      return out;
    }

    String lineText = trimTrailingSpacesCopy(text.substring(start, next));
    start = next;
    while (start < (int)text.length() && isSpaceByte(text[start])) start++;

    if (line == maxLines - 1 && start < (int)text.length()) {
      lineText = fitTextToWidth(u8g2f, lineText + " " + text.substring(start), maxWidth, truncMarker);
      out.lines[out.lineCount++] = lineText;
      out.truncated = true;
      return out;
    }

    out.lines[out.lineCount++] = lineText;
  }

  if (out.lineCount == 0) {
    out.lines[out.lineCount++] = fitTextToWidth(u8g2f, text, maxWidth, truncMarker);
    out.truncated = true;
  }
  return out;
}

static void renderSegment(U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                          const char* text,
                          const uint8_t* font,
                          uint16_t colour,
                          int16_t margin,
                          int16_t maxX,
                          int16_t& x,
                          int16_t& y,
                          int16_t lineH,
                          int16_t maxY)
{
  int16_t bottomY = y;
  bool overflow = false;
  bool phraseSeen = false;
  bool phraseFits = true;
  walkWrappedSegment(u8g2f, text, false, font, colour, true,
                     margin, maxX, x, y, lineH, maxY,
                     bottomY, overflow, phraseSeen, phraseFits);
}

} // namespace

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
                          bool* outQuoteFits,
                          bool* outPhraseFits,
                          uint8_t* outAttrLines,
                          bool* outAttrTruncated,
                          int16_t* outQuoteBottomY)
{
  display.init(115200, false, 20, false);
  display.setRotation(1);
  u8g2f.begin(display);
  u8g2f.setFont(kQuoteFontRegular);
  u8g2f.setBackgroundColor(GxEPD_WHITE);

  const int16_t ascent  = (int16_t)u8g2f.getFontAscent();
  const int16_t descent = (int16_t)u8g2f.getFontDescent();
  const int16_t lineH   = ascent - descent + 2;
  const int16_t startY  = margin + ascent;
  const int16_t maxY    = attrY - lineH - 2;

  String beforeText = before ? String(before) : String("");
  const String phraseText = phrase ? String(phrase) : String("");
  String afterText  = after  ? String(after)  : String("");
  QuoteLayoutMetrics quoteMetrics = measureQuoteLayout(
    u8g2f, beforeText, phraseText, afterText, margin, maxX, startY, lineH, maxY
  );

  struct VariantPair { const char* b; const char* a; };
  const VariantPair variants[2] = {
    { beforeTight, afterTight },
    { beforeCompact, afterCompact }
  };
  for (const VariantPair& v : variants) {
    if (quoteMetrics.quoteFits && quoteMetrics.phraseFits) break;
    if (!v.b && !v.a) continue;

    const String vb = v.b ? String(v.b) : String("");
    const String va = v.a ? String(v.a) : String("");
    QuoteLayoutMetrics vm = measureQuoteLayout(
      u8g2f, vb, phraseText, va, margin, maxX, startY, lineH, maxY
    );
    if (vm.quoteFits && vm.phraseFits) {
      beforeText = vb;
      afterText = va;
      quoteMetrics = vm;
      break;
    }
  }

  if (!quoteMetrics.quoteFits || !quoteMetrics.phraseFits) {
    fitQuoteToDisplay(u8g2f,
                      beforeText,
                      phraseText,
                      afterText,
                      margin,
                      maxX,
                      startY,
                      lineH,
                      maxY,
                      truncMarker,
                      quoteMetrics);
  }

  String attrText = String("-- ") + (attr ? attr : "");
  const bool attrFitsOneLine = (int16_t)u8g2f.getUTF8Width(attrText.c_str()) <= (maxX - margin);
  const bool canUseTwoAttrLines = quoteMetrics.bottomY <= (attrY - (2 * lineH) - 2);
  const uint8_t attrMaxLines = (!attrFitsOneLine && canUseTwoAttrLines) ? 2u : 1u;
  const AttrLayout attrLayout = layoutAttribution(u8g2f, attrText, attrMaxLines, maxX - margin, truncMarker);
  const int16_t attrStartY = attrY - (int16_t)(attrLayout.lineCount - 1u) * lineH;

  if (outQuoteFits) *outQuoteFits = quoteMetrics.quoteFits;
  if (outPhraseFits) *outPhraseFits = quoteMetrics.phraseFits;
  if (outAttrLines) *outAttrLines = attrLayout.lineCount;
  if (outAttrTruncated) *outAttrTruncated = attrLayout.truncated;
  if (outQuoteBottomY) *outQuoteBottomY = quoteMetrics.bottomY;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    int16_t x = margin;
    int16_t y = startY;
    renderSegment(u8g2f, beforeText.c_str(), kQuoteFontRegular, GxEPD_BLACK, margin, maxX, x, y, lineH, maxY);
    renderSegment(u8g2f, phraseText.c_str(), kQuoteFontPhrase, GxEPD_RED, margin, maxX, x, y, lineH, maxY);
    renderSegment(u8g2f, afterText.c_str(), kQuoteFontRegular, GxEPD_BLACK, margin, maxX, x, y, lineH, maxY);

    u8g2f.setForegroundColor(GxEPD_BLACK);
    for (uint8_t i = 0; i < attrLayout.lineCount; i++) {
      u8g2f.drawUTF8(margin, attrStartY + (int16_t)i * lineH, attrLayout.lines[i].c_str());
    }
  } while (display.nextPage());
}

[[noreturn]] void showFatalErrorScreenAndHalt(ClockDisplay& display,
                                              U8G2_FOR_ADAFRUIT_GFX& u8g2f,
                                              const char* code,
                                              const char* detail,
                                              int16_t margin,
                                              int16_t maxX,
                                              int16_t attrY)
{
  display.init(115200, false, 20, false);
  display.setRotation(1);
  u8g2f.begin(display);
  u8g2f.setFont(kQuoteFontRegular);
  u8g2f.setBackgroundColor(GxEPD_WHITE);

  const int16_t ascent = (int16_t)u8g2f.getFontAscent();
  const int16_t descent = (int16_t)u8g2f.getFontDescent();
  const int16_t lineH = ascent - descent + 2;
  const int16_t startY = margin + ascent;
  const int16_t maxY = attrY - lineH;

  const String hdr = "FATAL ERROR";
  const String c = String("Code: ") + (code ? code : "UNKNOWN");
  const String d = String("Detail: ") + (detail ? detail : "");

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    int16_t x = margin;
    int16_t y = startY;
    renderSegment(u8g2f, hdr.c_str(), kQuoteFontRegular, GxEPD_RED, margin, maxX, x, y, lineH, maxY);
    x = margin;
    y += lineH;
    renderSegment(u8g2f, c.c_str(), kQuoteFontRegular, GxEPD_BLACK, margin, maxX, x, y, lineH, maxY);
    x = margin;
    y += lineH;
    renderSegment(u8g2f, d.c_str(), kQuoteFontRegular, GxEPD_BLACK, margin, maxX, x, y, lineH, maxY);
    x = margin;
    y += lineH;
    renderSegment(u8g2f, "Reboot required", kQuoteFontRegular, GxEPD_BLACK, margin, maxX, x, y, lineH, maxY);
  } while (display.nextPage());

  display.hibernate();
  while (true) {
    delay(60000);
  }
}
