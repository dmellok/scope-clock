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

// --- more (faces_more.cpp) --------------------------------------------------
void solar(const ClockState&, DrawList&);      // planets, from the date
void moon(const ClockState&, DrawList&);       // phase, from the date
void pong(const ClockState&, DrawList&);       // plays itself
void life(const ClockState&, DrawList&);       // Conway, drawn as runs
void trailclock(const ClockState&, DrawList&); // hands with a phosphor wake
void weather(const ClockState&, DrawList&);    // host-fed
void ticker(const ClockState&, DrawList&);     // host-fed marquee
void worldclock(const ClockState&, DrawList&); // host-fed zone offsets
void asteroids(const ClockState&, DrawList&);  // plays itself

// --- wireframes (faces_wire.cpp) --------------------------------------------
void teapot(const ClockState&, DrawList&);     // the Utah teapot, as a profile
void sphere(const ClockState&, DrawList&);     // meridians and parallels
void knot(const ClockState&, DrawList&);       // trefoil
void mobius(const ClockState&, DrawList&);     // band with a half twist
void helix(const ClockState&, DrawList&);      // twin helix and rungs

// --- effects ----------------------------------------------------------------
void matrix(const ClockState&, DrawList&);     // digital rain
void atom(const ClockState&, DrawList&);       // Bohr model, all 118
void setAtomZ(uint8_t z);                      // 0 = cycle

// --- host-fed (faces_now.cpp) -----------------------------------------------
void nowplaying(const ClockState&, DrawList&); // track + progress ring
void gauges(const ClockState&, DrawList&);     // labelled percentage rings

// --- MIDI (faces_midi.cpp) --------------------------------------------------
void midiscope(const ClockState&, DrawList&);  // the interval as an X-Y figure
void midichord(const ClockState&, DrawList&);  // the chord as a shape on a wheel

// --- shared helpers ---------------------------------------------------------
inline int to12(int h) { return h == 0 ? 12 : (h > 12 ? h - 12 : h); }

}}  // namespace faces::impl
