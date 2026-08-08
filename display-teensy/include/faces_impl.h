// faces_impl.h — the individual face renderers.
//
// faces.cpp owns the registry and the knob/button navigation; the renderers
// themselves live in faces_time.cpp, faces_3d.cpp and faces_gen.cpp. One file
// of twenty-one faces was becoming impossible to edit safely.
//
// Everything here is a plain function of (time, list): a face never draws, it
// only describes. That is what lets the same list be centred, banner-overlaid
// and brightness-adapted before a single dot is placed.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
struct ClockState;
struct DrawList;

namespace faces { namespace impl {

// --- time -------------------------------------------------------------------
void hands(const ClockState&, DrawList&);      // Roman dial
void numbers(const ClockState&, DrawList&);    // numbered dial
void tickdial(const ClockState&, DrawList&);   // 60 ticks, baton hands
void orbit(const ClockState&, DrawList&);      // orrery: three bodies on rings
void sector(const ClockState&, DrawList&);     // three arcs, length = time
void digital(const ClockState&, DrawList&);
void datetime(const ClockState&, DrawList&);
void wordclock(const ClockState&, DrawList&);  // "IT IS TWENTY PAST TEN"
void binary(const ClockState&, DrawList&);     // BCD dots

// --- solids -----------------------------------------------------------------
void tetra(const ClockState&, DrawList&);
void cube(const ClockState&, DrawList&);
void octa(const ClockState&, DrawList&);
void icosa(const ClockState&, DrawList&);
void dodeca(const ClockState&, DrawList&);
void tesseract(const ClockState&, DrawList&);  // 4D, projected twice
void torus(const ClockState&, DrawList&);

// --- generative -------------------------------------------------------------
void lissajous(const ClockState&, DrawList&);
void harmonograph(const ClockState&, DrawList&);
void spirograph(const ClockState&, DrawList&);
void rose(const ClockState&, DrawList&);
void lorenz(const ClockState&, DrawList&);
void starpoly(const ClockState&, DrawList&);   // {n/k} star polygons
void starfield(const ClockState&, DrawList&);
void tunnel(const ClockState&, DrawList&);

// --- shared helpers ---------------------------------------------------------
inline int to12(int h) { return h == 0 ? 12 : (h > 12 ? h - 12 : h); }

}}  // namespace faces::impl
