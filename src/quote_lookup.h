#pragma once

#include <Arduino.h>
#include <ctype.h>
#include <stdint.h>
#include <esp_random.h>

namespace QuoteLookup {

template <typename QuoteT>
inline bool hasRenderablePhrase(const QuoteT* quotes, uint16_t quoteCount, uint16_t idx)
{
  if (!quotes || idx >= quoteCount) return false;
  const char* p = quotes[idx].phrase;
  if (!p) return false;
  while (*p) {
    if (!isspace((unsigned char)*p)) return true;
    p++;
  }
  return false;
}

template <typename QuoteT>
inline bool phraseLooks24Hour(const QuoteT* quotes, uint16_t quoteCount, uint16_t idx)
{
  if (!quotes || idx >= quoteCount) return false;
  const char* p = quotes[idx].phrase;
  if (!p || !*p) return false;

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

  for (const char* q = p; q[4] != '\0'; ++q) {
    if (!isdigit((unsigned char)q[0]) || !isdigit((unsigned char)q[1]) ||
        q[2] != ':' ||
        !isdigit((unsigned char)q[3]) || !isdigit((unsigned char)q[4])) {
      continue;
    }

    const int hh = (q[0] - '0') * 10 + (q[1] - '0');
    const int mm = (q[3] - '0') * 10 + (q[4] - '0');
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) continue;

    if (q[5] == '\0' || !isdigit((unsigned char)q[5])) return true;
    if (q[5] == ':' && isdigit((unsigned char)q[6]) && isdigit((unsigned char)q[7])) {
      const int ss = (q[6] - '0') * 10 + (q[7] - '0');
      if (ss >= 0 && ss <= 59) return true;
    }
  }

  return false;
}

template <typename QuoteT>
inline uint16_t findQuote(uint16_t minute, const QuoteT* quotes, uint16_t quoteCount)
{
  if (!quotes || quoteCount == 0) return 0u;

  for (int radius = 0; radius <= 60; radius++) {
    const int offsets[2] = { (radius == 0) ? 0 : -radius, radius };
    const int nCheck     = (radius == 0) ? 1 : 2;

    for (int oi = 0; oi < nCheck; oi++) {
      const int target = ((int)minute + offsets[oi] + 1440) % 1440;

      int lo = 0, hi = (int)quoteCount - 1;
      while (lo < hi) {
        const int mid = (lo + hi) / 2;
        ((int)quotes[mid].minute < target) ? (lo = mid + 1) : (hi = mid);
      }

      if ((int)quotes[lo].minute == target) {
        int start = lo, count = 0;
        while (start + count < (int)quoteCount &&
               (int)quotes[start + count].minute == target) count++;

        for (int tries = 0; tries < count; tries++) {
          const uint16_t pick = (uint16_t)(start + (int)(esp_random() % (uint32_t)count));
          if (hasRenderablePhrase(quotes, quoteCount, pick) &&
              phraseLooks24Hour(quotes, quoteCount, pick)) return pick;
        }
        for (int i = 0; i < count; i++) {
          const uint16_t pick = (uint16_t)(start + i);
          if (hasRenderablePhrase(quotes, quoteCount, pick) &&
              phraseLooks24Hour(quotes, quoteCount, pick)) return pick;
        }

        for (int tries = 0; tries < count; tries++) {
          const uint16_t pick = (uint16_t)(start + (int)(esp_random() % (uint32_t)count));
          if (hasRenderablePhrase(quotes, quoteCount, pick)) return pick;
        }
        for (int i = 0; i < count; i++) {
          const uint16_t pick = (uint16_t)(start + i);
          if (hasRenderablePhrase(quotes, quoteCount, pick)) return pick;
        }
      }
    }
  }

  return 0u;
}

template <typename QuoteT>
inline uint16_t initLookupTable(uint16_t outTable[1440], const QuoteT* quotes, uint16_t quoteCount)
{
  if (!outTable || !quotes || quoteCount == 0) return 0u;

  uint16_t renderableCount = 0;
  for (int m = 0; m < 1440; m++) {
    const uint16_t idx = findQuote((uint16_t)m, quotes, quoteCount);
    outTable[m] = idx;
    if (hasRenderablePhrase(quotes, quoteCount, idx)) renderableCount++;
  }
  return renderableCount;
}

} // namespace QuoteLookup
