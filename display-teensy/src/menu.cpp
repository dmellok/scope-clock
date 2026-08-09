// menu.cpp — the settings list, reached by holding the knob's button.
//
// Everything reachable here is LOCAL. That is the selection rule: a setting
// belongs in this menu if changing it means something with no bridge attached,
// because a menu on the device is only worth having when the device is alone.
// Wi-Fi, MQTT and the rest stay on the config page where they belong.
//
// Drawn as a jog dial rather than a list. Seven labels stacked down a round
// tube would have the top and bottom ones running off the chord, and a rotary
// encoder does not read as "scroll a page" anyway — it reads as "the thing in
// the middle is the one you have got". So three rows, the middle one large,
// and the selection stays put while the labels move past it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "menu.h"
#include "state.h"
#include "drawlist.h"
#include "text.h"
#include "vector.h"
#include "settime.h"
#include <Arduino.h>
#include <stdio.h>

namespace {

// Rows, top to bottom. Kept clear of the rim: at y = 900 the chord is only
// about 790 either side, and centredFit shrinks a label to suit rather than
// letting its ends fall off the glass.
constexpr int kTitleY = 900;
constexpr int kPrevY  = 420;
constexpr int kCurrY  = 40;
constexpr int kValY   = -280;
constexpr int kNextY  = -560;
constexpr int kHintY  = -930;

const char* const kLabel[Menu::kCount] = {
  "SET TIME", "SET DATE", "FACE SIZE", "TYPEFACE", "BURN-IN DRIFT",
  "INFO", "EXIT",
};

// Same order and spelling as kFaces[] in text.cpp and kFontNames[] on the
// bridge; tools/check_faces.py holds those two together, and this is a third
// copy only in the sense that it is the same six words.
const char* const kFontName[6] = {
  "REGULAR", "SEVEN SEG", "CONDENSED", "WIDE", "ITALIC", "BOLD",
};

}  // namespace

// The value shown under an entry, or nullptr for the ones that just do a thing.
// Static buffers: an Item keeps the pointer it is handed.
const char* Menu::valueFor(uint8_t item) {
  static char buf[24];
  switch (item) {
    case Menu::Typeface: {
      const uint8_t f = txt::defaultFaceId();
      return kFontName[f < 6 ? f : 0];
    }
    case Menu::Drift:
      return vec::wobble() ? "ON" : "OFF";
    case Menu::Info: {
      snprintf(buf, sizeof buf, "UP %lus", (unsigned long)(millis() / 1000));
      return buf;
    }
    default:
      return nullptr;
  }
}

void Menu::activate(DeviceState& dev) {
  switch (dev.menuItem) {
    case Menu::SetTime:
      dev.edit = DeviceState::Edit::Time;
      dev.editField = 0;
      dev.editSeed = true;              // the main loop owns the RTC
      dev.editCommit = false;
      dev.menuMode = false;
      break;
    case Menu::SetDate:
      dev.edit = DeviceState::Edit::Date;
      dev.editField = 0;
      dev.editSeed = true;
      dev.editCommit = false;
      dev.menuMode = false;
      break;
    case Menu::FaceSize:
      // Hands the knob straight to the size it already knew how to adjust,
      // rather than duplicating that logic here.
      dev.menuMode = false;
      dev.scaleMode = true;
      break;
    case Menu::Typeface:
      txt::setDefaultFace((uint8_t)((txt::defaultFaceId() + 1) % txt::faceCount()));
      dev.fontChanged = true;           // so a bridge, if there is one, remembers
      break;
    case Menu::Drift:
      vec::setWobble(!vec::wobble());
      dev.wobbleChanged = true;
      break;
    case Menu::Info:
      break;                            // the value line is the whole entry
    case Menu::Exit:
    default:
      dev.menuMode = false;
      break;
  }
}

void overlayMenu(DeviceState& dev, DrawList& list) {
  if (!dev.menuMode) return;
  list.clear();

  const uint8_t n = Menu::kCount;
  const uint8_t cur = dev.menuItem < n ? dev.menuItem : 0;
  const uint8_t prev = (uint8_t)((cur + n - 1) % n);
  const uint8_t next = (uint8_t)((cur + 1) % n);

  txt::centredFit(list, kTitleY, 6, "SETTINGS");
  txt::centredFit(list, kPrevY, 6, kLabel[prev]);

  // The selected row, with markers either side. One buffer, because an Item
  // keeps the pointer and three rows sharing one would all show the last.
  static char sel[40];
  snprintf(sel, sizeof sel, "> %s <", kLabel[cur]);
  txt::centredFit(list, kCurrY, 10, sel);

  const char* v = Menu::valueFor(cur);
  if (v) txt::centredFit(list, kValY, 8, v);

  txt::centredFit(list, kNextY, 6, kLabel[next]);
  txt::centredFit(list, kHintY, 5, "TURN - TAP TO SELECT");
}
