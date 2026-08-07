// hal/dac.h — X/Y position + Z blank. Board variants live behind this header.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace hal { namespace dac {
  void init();
  void write(int x, int y);   // 12-bit range
  void blank(bool on);        // true = beam off
}}
