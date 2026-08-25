// fake/Arduino.h — just enough Arduino for the real render path to build on a
// host, so geometry can be checked without flashing.
//
// Two things here are load-bearing and were both learned the hard way:
//
//  * millis() must ADVANCE. Returning a constant froze every face driven by
//    time — the ticker measured as an empty window, the now-playing ring never
//    moved, the wobble never drifted — so a time-driven face measured as if it
//    were a still image. The harness steps it one frame per rendered frame.
//
//  * ARM_DWT_CYCCNT must advance too. vec::dwell() spins on it until the beam
//    has held a dot for dotDwell cycles (120 by default, not 0), so a constant
//    here hangs the harness outright rather than failing visibly.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

uint32_t simMillis();
void     simStepMillis(uint32_t ms);
void     simSetMillis(uint32_t ms);   // to replay a specific frame
uint32_t simCycles();          // advances on every read; see above

extern uint32_t simDemcr, simDwtCtrl;

#define ARM_DWT_CYCCNT          (simCycles())
#define ARM_DEMCR               simDemcr
#define ARM_DWT_CTRL            simDwtCtrl
#define ARM_DEMCR_TRCENA        (1u << 24)
#define ARM_DWT_CTRL_CYCCNTENA  (1u << 0)

inline uint32_t millis() { return simMillis(); }
inline uint32_t micros() { return simMillis() * 1000u; }
inline void delayMicroseconds(unsigned) {}
inline void delay(unsigned) {}
inline void analogWrite(int, int) {}
