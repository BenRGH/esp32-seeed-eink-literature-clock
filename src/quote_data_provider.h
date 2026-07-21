#pragma once

#include <stdint.h>

// Centralized quote dataset wiring:
// - Uses generated src/litclock_data.h when present.
// - Falls back to a tiny built-in stub so firmware still compiles.
#if __has_include("litclock_data.h")
  #include "litclock_data.h"
#else
  #warning "litclock_data.h not found - 12-quote stub active. Run .\\generate_litclock_data.ps1"
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