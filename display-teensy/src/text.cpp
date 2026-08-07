// text.cpp — ported from SCTVcode b_font.ino (the 12x20 vector font) and
// d_drawing.ino (GetSeg / DispStr / GetWid / Center).
//
// This is a *stroke* font: every glyph is a short list of lines and arcs in a
// 12-wide, 20-tall cell whose origin is the lower-left corner. Nothing here is
// rasterised — the segments go straight to the beam.
//
// Glyph encoding, seven signed values per segment:
//     lin, XStart, YStart, XEnd,  YEnd,  -,      -
//     cir, XCenter,YCenter,XSize, YSize, FirstO, LastO
// terminated by a single value with bit 7 set, whose low bits are the advance
// width of the glyph.  Octants: 0 = East, counter-clockwise, lastO inclusive,
// so 6..13 is a full circle starting at South.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "text.h"
#include "drawlist.h"
#include "vector.h"
#include <stdint.h>

namespace txt {
namespace {

enum : int16_t { lin = 0, cir = 1 };

// This font's metrics. Two character spacings: tight for big text, looser for
// small, because small text needs proportionally more air to stay legible.
constexpr int kBigKern = 2;    // kern for scale 40 and up
constexpr int kLilKern = 3;    // kern for scale 39 and down
constexpr int kBigGap  = 4;    // inter-row gap for scale 40 and up
constexpr int kLilGap  = 10;   // inter-row gap for scale 39 and down
constexpr int kChrHt   = 20;   // cell height

// --------------------------- the glyphs ----------------------------------
// circle:              cir,XC,YC,XS,YS,FO,LO{,width|0x80}
// line:                lin,XS,YS,XE,YE,FO,LO{,width|0x80}
const int16_t Space[]   = {0x86};      // space does nothing but move over
const int16_t Exclam[]  = { lin, 1, 6, 1,20, 6,13,
                            cir, 1, 1, 2, 2, 6,13, 0x82};
const int16_t DQuot[]   = { lin, 0,12, 0,20, 6,13,
                            lin, 6,12, 6,20, 6,13, 0x86};
const int16_t Sharp[]   = { lin, 2, 4, 4,20, 6,13,
                            lin, 7, 4, 9,20, 6,13,
                            lin, 0, 9,10, 9, 6,13,
                            lin, 1,15,11,15, 6,13, 0x8c};
const int16_t Dollar[]  = { cir, 5, 6,10, 8, 4, 9,
                            cir, 5,14,10, 8, 0, 5,
                            lin, 5, 2, 5,22, 6,13, 0x8a};
const int16_t Percent[] = { cir, 3,17, 6, 6, 6,13,
                            cir, 9, 3, 6, 6, 6,13,
                            lin, 3, 0,15,20, 6,13, 0x8c};
const int16_t Amper[]   = { cir, 4,15, 8,10, 6,12,
                            cir, 4, 5, 8,10, 2, 5,
                            cir, 4, 8,16,16, 6, 7,
                            lin, 1,12,11, 0, 6,13, 0x8c};
const int16_t Apost[]   = { cir, 3,19, 2, 2, 6,13,
                            cir, 0,19, 8,12, 6, 7, 0x84};
const int16_t LParen[]  = { cir, 4,10, 8,20, 2, 5, 0x84};
const int16_t RParen[]  = { cir, 0,10, 8,20, 6, 9, 0x84};
const int16_t Aster[]   = { lin, 0,10,12,10, 6,13,
                            lin, 2, 4,10,16, 6,13,
                            lin, 2,16,10, 4, 6,13, 0x8c};
const int16_t Plus[]    = { lin, 0,10,12,10, 6,13,
                            lin, 6, 4, 6,16, 6,13, 0x8c};
const int16_t Comma[]   = { cir, 3, 1, 2, 2, 6,13,
                            cir, 0, 1, 8,12, 6, 7, 0x84};
const int16_t Minus[]   = { lin, 0,10,12,10, 6,13, 0x8c};
const int16_t Period[]  = { cir, 1, 1, 2, 2, 6,13, 0x82};
const int16_t Slash[]   = { lin, 0, 0,12,20, 6,13, 0x8c};
const int16_t Zero[]    = { cir, 6,10,12,20, 6,13, 0x8c};
const int16_t One[]     = { lin, 7, 0, 7,20, 6,13,
                            lin, 3,16, 7,20, 6,13, 0x8c};
const int16_t Two[]     = { cir, 6,14,12,12, 6,11,
                            cir, 6, 0,12,16, 2, 3,
                            lin, 0, 0,12, 0, 6,13, 0x8c};
const int16_t Three[]   = { cir, 6, 6,12,12, 5, 9,
                            lin, 1,20,11,20, 6,13,
                            lin, 6,12,11,20, 6,13, 0x8c};
const int16_t Four[]    = { lin, 8, 0, 8,20, 6,13,
                            lin, 0, 6,12, 6, 6,13,
                            lin, 0, 6, 8,20, 6,13, 0x8c};
const int16_t Five[]    = { cir, 6, 6,12,12, 5,10,
                            lin, 2,10, 4,20, 6,13,
                            lin, 4,20,12,20, 6,13, 0x8c};
const int16_t Six[]     = { cir, 6, 6,12,12, 6,13,
                            lin, 1,10, 8,20, 6,13, 0x8c};
const int16_t Seven[]   = { lin, 0, 0,12,20, 6,13,
                            lin, 0,20,12,20, 6,13, 0x8c};
const int16_t Eight[]   = { cir, 6, 6,12,12, 6,13,
                            cir, 6,16, 8, 8, 6,13, 0x8c};
const int16_t Nine[]    = { cir, 6,14,12,12, 6,13,
                            lin, 4, 0,11,10, 6,13, 0x8c};
const int16_t Colon[]   = { cir, 2, 6, 4, 4, 6,13,
                            cir, 2,14, 4, 4, 6,13, 0x84};
const int16_t SemiCol[] = { cir, 3,14, 2, 2, 6,13,
                            cir, 3, 6, 2, 2, 6,13,
                            cir, 0, 6, 8,12, 6, 7, 0x84};
const int16_t LThan[]   = { lin, 0,10,12,18, 6,13,
                            lin, 0,10,12, 2, 6,13, 0x8c};
const int16_t Equal[]   = { lin, 0,13,12,13, 6,13,
                            lin, 0, 7,12, 7, 6,13, 0x8c};
const int16_t GThan[]   = { lin, 0,18,12,10, 6,13,
                            lin, 0, 2,12,10, 6,13, 0x8c};
const int16_t Quest[]   = { cir, 5,14,10,10, 6,11,
                            cir, 5, 7, 4, 4, 2, 7,
                            cir, 5, 1, 2, 2, 6,13, 0x8a};
const int16_t AtSign[]  = { cir, 3,10, 6,10, 6,13,
                            cir, 3,10,14,20, 0, 6,
                            cir, 8,10, 4, 4, 4, 7, 0x8c};
const int16_t BigA[]    = { lin, 0, 0, 6,20, 6,13,
                            lin, 6,20,12, 0, 6,13,
                            lin, 3, 8, 9, 8, 6,13, 0x8c};
const int16_t BigB[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0, 0, 8, 0, 6,13,
                            lin, 0,10, 8,10, 6,13,
                            lin, 0,20, 8,20, 6,13,
                            cir, 8, 5,10,10, 6, 9,
                            cir, 8,15,10,10, 6, 9, 0x8c};
const int16_t BigC[]    = { cir, 7,10,14,20, 1, 6, 0x8c};
const int16_t BigD[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0, 0, 4, 0, 6,13,
                            lin, 0,20, 4,20, 6,13,
                            cir, 4,10,16,20, 6, 9, 0x8c};
const int16_t BigE[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0, 0,12, 0, 6,13,
                            lin, 0,10, 8,10, 6,13,
                            lin, 0,20,12,20, 6,13, 0x8c};
const int16_t BigF[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0,10, 8,10, 6,13,
                            lin, 0,20,12,20, 6,13, 0x8c};
const int16_t BigG[]    = { cir, 7,10,14,20, 1, 6,
                            lin,11, 2,11, 8, 6,13,
                            lin, 7, 8,11, 8, 6,13, 0x8c};
const int16_t BigH[]    = { lin, 0, 0, 0,20, 6,13,
                            lin,12, 0,12,20, 6,13,
                            lin, 0,10,12,10, 6,13, 0x8c};
const int16_t BigI[]    = { lin, 2, 0, 2,20, 6,13,
                            lin, 1, 0, 5, 0, 6,13,
                            lin, 1,20, 5,20, 6,13, 0x84};
const int16_t BigJ[]    = { lin,12, 6,12,20, 6,13,
                            cir, 6, 6,12,12, 4, 7, 0x8c};
const int16_t BigK[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0,10,12, 0, 6,13,
                            lin, 0,10,12,20, 6,13, 0x8c};
const int16_t BigL[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0, 0,12, 0, 6,13, 0x8c};
const int16_t BigM[]    = { lin, 0, 0, 0,20, 6,13,
                            lin,12, 0,12,20, 6,13,
                            lin, 0,20, 6,10, 6,13,
                            lin, 6,10,12,20, 6,13, 0x8c};
const int16_t BigN[]    = { lin, 0, 0, 0,20, 6,13,
                            lin,12, 0,12,20, 6,13,
                            lin, 0,20,12, 0, 6,13, 0x8c};
const int16_t BigO[]    = { cir, 6,10,12,20, 6,13, 0x8c};
const int16_t BigP[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0,10, 8,10, 6,13,
                            lin, 0,20, 8,20, 6,13,
                            cir, 8,15,10,10, 6, 9, 0x8c};
const int16_t BigQ[]    = { cir, 6,10,12,20, 6,13,
                            lin, 8, 6,12, 0, 6,13, 0x8c};
const int16_t BigR[]    = { lin, 0, 0, 0,20, 6,13,
                            lin, 0,10, 8,10, 6,13,
                            lin, 0,20, 8,20, 6,13,
                            cir, 8,15,10,10, 6, 9,
                            lin, 6,10,12, 0, 6,13, 0x8c};
const int16_t BigS[]    = { cir, 6, 5,12,10, 4, 9,
                            cir, 6,15,12,10, 0, 5, 0x8c};
const int16_t BigT[]    = { lin, 6, 0, 6,20, 6,13,
                            lin, 0,20,12,20, 6,13, 0x8c};
const int16_t BigU[]    = { lin, 0, 6, 0,20, 6,13,
                            lin,12, 6,12,20, 6,13,
                            cir, 6, 6,12,12, 4, 7, 0x8c};
const int16_t BigV[]    = { lin, 0,20, 6, 0, 6,13,
                            lin, 6, 0,12,20, 6,13, 0x8c};
const int16_t BigW[]    = { lin, 0, 0, 0,20, 6,13,
                            lin,12, 0,12,20, 6,13,
                            lin, 0, 0, 6,10, 6,13,
                            lin, 6,10,12, 0, 6,13, 0x8c};
const int16_t BigX[]    = { lin, 0,20,12, 0, 6,13,
                            lin, 0, 0,12,20, 6,13, 0x8c};
const int16_t BigY[]    = { lin, 6, 0, 6,10, 6,13,
                            lin, 0,20, 6,10, 6,13,
                            lin, 6,10,12,20, 6,13, 0x8c};
const int16_t BigZ[]    = { lin, 0, 0,12,20, 6,13,
                            lin, 0, 0,12, 0, 6,13,
                            lin, 0,20,12,20, 6,13, 0x8c};
const int16_t LftSqBr[] = { lin, 0, 0, 0,20, 6,13,
                            lin, 0, 0, 4, 0, 6,13,
                            lin, 0,20, 4,20, 6,13, 0x84};
const int16_t BackSl[]  = { lin, 0,20,12, 0, 6,13, 0x8c};
const int16_t RtSqBr[]  = { lin, 4, 0, 4,20, 6,13,
                            lin, 0, 0, 4, 0, 6,13,
                            lin, 0,20, 4,20, 6,13, 0x84};
const int16_t Carat[]   = { lin, 0,10, 6,16, 6,13,
                            lin, 6,16,12,10, 6,13, 0x8c};
const int16_t UnderSc[] = { lin, 0, 0,12, 0, 6,13, 0x8c};
const int16_t BackQu[]  = { lin, 0,20, 4,12, 6,13, 0x84};
const int16_t SmallA[]  = { cir, 5, 6,10,12, 6,13,
                            lin,10, 0,10,12, 6,13, 0x8a};
const int16_t SmallB[]  = { cir, 5, 6,10,12, 6,13,
                            lin, 0, 0, 0,20, 6,13, 0x8a};
const int16_t SmallC[]  = { cir, 5, 6,10,12, 1, 6, 0x88};
const int16_t SmallD[]  = { cir, 5, 6,10,12, 6,13,
                            lin,10, 0,10,20, 6,13, 0x8a};
const int16_t SmallE[]  = { cir, 5, 6,10,12, 0, 6,
                            lin, 0, 6,10, 6, 6,13, 0x8a};
const int16_t SmallF[]  = { cir, 7,16, 6, 8, 0, 3,
                            lin, 0,10, 8,10, 6,13,
                            lin, 4, 0, 4,16, 6,13, 0x8a};
const int16_t SmallG[]  = { cir, 5, 6,10,12, 6,13,
                            lin,10, 0,10,12, 6,13,
                            cir, 5, 0,10,12, 5, 7, 0x8a};
const int16_t SmallH[]  = { cir, 4, 8, 8, 8, 0, 3,
                            lin, 0, 0, 0,20, 6,13,
                            lin, 8, 0, 8, 8, 6,13, 0x88};
const int16_t SmallI[]  = { cir, 1,16, 2, 2, 6,13,
                            lin, 1, 0, 1,12, 6,13, 0x82};
const int16_t SmallJ[]  = { cir, 6,16, 2, 2, 6,13,
                            lin, 6, 0, 6,12, 6,13,
                            cir, 3, 0, 6, 8, 5, 7, 0x88};
const int16_t SmallK[]  = { lin, 0, 0, 0,20, 6,13,
                            lin, 0, 4, 8,12, 6,13,
                            lin, 1, 6, 7, 0, 6,13, 0x88};
const int16_t SmallL[]  = { lin, 1, 0, 1,20, 6,13, 0x82};
const int16_t SmallM[]  = { lin, 0, 0, 0,12, 6,13,
                            cir, 4, 8, 8, 8, 0, 3,
                            lin, 8, 0, 8, 8, 6,13,
                            cir,12, 8, 8, 8, 0, 3,
                            lin,16, 0,16, 8, 6,13, 0x90};
const int16_t SmallN[]  = { lin, 0, 0, 0,12, 6,13,
                            cir, 4, 8, 8, 8, 0, 3,
                            lin, 8, 0, 8, 8, 6,13, 0x88};
const int16_t SmallO[]  = { cir, 5, 6,10,12, 6,13, 0x8a};
const int16_t SmallP[]  = { cir, 5, 6,10,12, 6,13,
                            lin, 0,-4, 0,12, 6,13, 0x8a};
const int16_t SmallQ[]  = { cir, 5, 6,10,12, 6,13,
                            lin,10,-4,10,12, 6,13, 0x8a};
const int16_t SmallR[]  = { lin, 0, 0, 0,12, 6,13,
                            cir, 5, 6,10,12, 1, 3, 0x88};
const int16_t SmallS[]  = { cir, 4, 9, 8, 6, 0, 5,
                            cir, 4, 3, 8, 6, 4, 9, 0x88};
const int16_t SmallT[]  = { lin, 0,12, 8,12, 6,13,
                            lin, 4, 0, 4,16, 6,13, 0x88};
const int16_t SmallU[]  = { lin, 8, 0, 8,12, 6,13,
                            cir, 4, 4, 8, 8, 4, 7,
                            lin, 0, 4, 0,12, 6,13, 0x88};
const int16_t SmallV[]  = { lin, 0,12, 4, 0, 6,13,
                            lin, 4, 0, 8,12, 6,13, 0x88};
const int16_t SmallW[]  = { lin, 0,12, 4, 0, 6,13,
                            lin, 4, 0, 8,12, 6,13,
                            lin, 8,12,12, 0, 6,13,
                            lin,12, 0,16,12, 6,13, 0x90};
const int16_t SmallX[]  = { lin, 0,12, 8, 0, 6,13,
                            lin, 0, 0, 8,12, 6,13, 0x88};
const int16_t SmallY[]  = { lin, 0,12, 4, 0, 6,13,
                            lin, 4, 0, 8,12, 6,13,
                            cir, 1, 0, 6, 8, 6, 7, 0x88};
const int16_t SmallZ[]  = { lin, 0, 0, 8, 0, 6,13,
                            lin, 0,12, 8,12, 6,13,
                            lin, 0, 0, 8,12, 6,13, 0x88};
const int16_t LfBrace[] = { cir, 8, 6, 8,12, 4, 5,
                            cir, 0, 6, 8, 8, 0, 1,
                            cir, 0,14, 8, 8, 6, 7,
                            cir, 8,14, 8,12, 2, 3, 0x88};
const int16_t VertBar[] = { lin, 1, 1, 1,21, 6,13, 0x82};
const int16_t RtBrace[] = { cir, 0, 6, 8,12, 6, 7,
                            cir, 8, 6, 8, 8, 2, 3,
                            cir, 8,14, 8, 8, 4, 5,
                            cir, 0,14, 8,12, 0, 1, 0x88};
const int16_t Tilde[]   = { cir, 3,12, 6, 4, 0, 3,
                            cir, 9,12, 6, 4, 4, 7, 0x8c};
const int16_t Rubout[]  = { lin, 0,10, 6,20, 6,13,
                            lin, 0, 0,12,20, 6,13,
                            lin, 6, 0,12,10, 6,13, 0x8c};

// All the characters, arranged in ASCII order starting at 0x20.
const int16_t* const Font[] =
{Space, Exclam,DQuot, Sharp,  Dollar, Percent,Amper, Apost,
 LParen,RParen,Aster, Plus,   Comma,  Minus,  Period,Slash,
 Zero,  One,   Two,   Three,  Four,   Five,   Six,   Seven,
 Eight, Nine,  Colon, SemiCol,LThan,  Equal,  GThan, Quest,
 AtSign,BigA,  BigB,  BigC,   BigD,   BigE,   BigF,  BigG,
 BigH,  BigI,  BigJ,  BigK,   BigL,   BigM,   BigN,  BigO,
 BigP,  BigQ,  BigR,  BigS,   BigT,   BigU,   BigV,  BigW,
 BigX,  BigY,  BigZ,  LftSqBr,BackSl, RtSqBr, Carat, UnderSc,
 BackQu,SmallA,SmallB,SmallC, SmallD, SmallE, SmallF,SmallG,
 SmallH,SmallI,SmallJ,SmallK, SmallL, SmallM, SmallN,SmallO,
 SmallP,SmallQ,SmallR,SmallS, SmallT, SmallU, SmallV,SmallW,
 SmallX,SmallY,SmallZ,LfBrace,VertBar,RtBrace,Tilde, Rubout};

constexpr int kSegLen = 7;   // values per segment

inline const int16_t* glyph(char ch) { return Font[(ch & 0x7f) - 32]; }

// SetScale(): derive the scaled metrics for a given scale factor.
struct Metrics { int chrHt, kern, rowGap; };
Metrics metrics(int scale) {
  Metrics m;
  m.chrHt = scale * kChrHt;
  m.kern   = scale * (scale < 40 ? kLilKern : kBigKern);
  m.rowGap = scale * (scale < 40 ? kLilGap  : kBigGap);
  return m;
}

// GetWid(): scaled width of one string, stopping at '\n' or NUL. `newline` is
// set when the string ended with '\n', which is how a row break is marked; the
// original drops one kern in that case so a row has n-1 gaps, not n.
int strWidth(int scale, const char* s, bool& newline) {
  int cells = 0, count = 0;
  char ch = 0;
  if (s) {
    while ((ch = *s++) >= 0x20) {
      const int16_t* seg = glyph(ch);
      while (*seg < 0x80) seg += kSegLen;   // skip segments, land on the width
      cells += *seg & 0x7f;
      count++;
    }
  }
  const Metrics m = metrics(scale);
  newline = (ch == '\n');
  return cells * scale + m.kern * (newline ? count - 1 : count);
}

} // namespace

// DispStr(): walk the string, emit each glyph's segments at the running pen
// position. Stops at any control character, so '\n' terminates a string.
void drawString(int x, int y, int scale, const char* s) {
  if (!s || scale <= 0) return;
  const Metrics m = metrics(scale);
  int penX = x;

  for (char ch = *s++; ch >= 0x20; ch = *s++) {
    const int16_t* seg = glyph(ch);
    for (;;) {
      const int16_t v = *seg++;
      if (v >= 0x80) {                       // end flag: low bits are the width
        penX += (v & 0x7f) * scale + m.kern;
        break;
      }
      const int a = *seg++;          // XStart  / XCenter
      const int b = *seg++;          // YStart  / YCenter
      const int c = *seg++;          // XEnd    / XSize
      const int d = *seg++;          // YEnd    / YSize
      const int firstO = *seg++;     // arcs only
      const int lastO  = *seg++;
      if (v == lin)
        vec::line(penX + a * scale, y + b * scale,
                  penX + c * scale, y + d * scale);
      else
        vec::ellipseArc(penX + a * scale, y + b * scale,
                        (c * scale) / 2, (d * scale) / 2, firstO, lastO);
    }
  }
}

int measure(int scale, const char* s) {
  bool nl;
  return strWidth(scale, s, nl);
}

// Center(): fill in x/y for text items so the block sits centred on (0,0).
//
// Tricky, because one row of text is several items (the digital face draws
// hours, colon and minutes as separate items so they can differ in scale), and
// only the last item of a row carries the '\n'. The total row width is only
// known after that last item, but the first item needs it — so pass 1 measures
// and records row widths, pass 2 lays them out.
//
// Items with a position already set opt the whole list out of centring, which
// is how the analog face keeps its hand-placed numerals.
void centerLines(DrawList& list) {
  const int n = list.count;
  if (n == 0) return;

  for (int i = 0; i < n; ++i) {
    const Item& it = list.items[i];
    if (it.type == ItemType::Text && (it.x != 0 || it.y != 0)) return;
  }

  int widths[DrawList::CAP];   // per item, scaled
  bool endsRow[DrawList::CAP];
  int rowWidths[DrawList::CAP];
  int rows = 0, rowWidth = 0, dispHt = 0;
  Metrics m = metrics(list.items[0].scale);

  // pass 1 — measure every item, accumulate row widths and total height
  for (int i = 0; i < n; ++i) {
    const Item& it = list.items[i];
    if (it.type != ItemType::Text) { widths[i] = 0; endsRow[i] = false; continue; }
    m = metrics(it.scale);
    widths[i] = strWidth(it.scale, it.str, endsRow[i]);
    rowWidth += widths[i];
    if (endsRow[i]) {
      rowWidths[rows++] = rowWidth;
      dispHt += m.rowGap + m.chrHt;   // row height, for the vertical centring
      rowWidth = 0;
    }
  }
  if (rows == 0) return;              // nothing terminated a row: nothing to do

  // pass 2 — place. Y starts half the block height above centre and walks down.
  int penY = (dispHt - m.rowGap) / 2;
  int penX = 0;
  int row = 0;
  bool startOfRow = true;

  for (int i = 0; i < n; ++i) {
    Item& it = list.items[i];
    if (it.type != ItemType::Text) continue;
    if (startOfRow) {
      penX = -(row < rows ? rowWidths[row] : 0) / 2;   // left end = -width/2
      row++;
      m = metrics(it.scale);
      penY -= m.chrHt;
    }
    startOfRow = endsRow[i];
    it.x = (int16_t)penX;
    it.y = (int16_t)penY;
    penX += widths[i];
    if (startOfRow) penY -= m.rowGap;   // n rows have n-1 gaps between them
  }
}

} // namespace txt
