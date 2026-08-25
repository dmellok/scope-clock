// webui.h — the control panel the bridge serves, held in flash.
//
// Everything is inline: no CDN, no external font, no second request. The clock
// may well live on a network with no route to the internet, and a control page
// that only works when GitHub is reachable is not a control page.
//
// Deliberately NOT skinned as a CRT. The clock itself is the beautiful object;
// scanlines and flicker on the control panel were costume rather than design,
// and they made real state — a warning, a stale reading — harder to see. What
// is left is a dark instrument panel: one restrained accent, monospace reserved
// for values so numbers line up, and prose in the system UI font.
//
// Icons are Phosphor (https://phosphoricons.com), MIT licensed, embedded as an
// SVG sprite. Which is a better joke than it looks: phosphor is also what the
// tube is coated in.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <pgmspace.h>

static const char WEB_UI[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>Scope Clock</title>
<style>
:root{
  /* Direction 2a. Surfaces run darkest-page to lighter-chrome, which is the
     inverse of the usual card stack: the drawer and the tube strip are the
     furniture, the face page is the thing being looked at. */
  --page:#08090f; --chrome:#0b0d13; --furn:#0a0c11; --input:#0d1017; --hover:#10151c;
  --line:#1a212b; --ctl:#232b36;
  --text:#e6ecf2; --text2:#c3ccd7; --body:#8a96a5; --cap:#5f6b7a; --count:#4c5765;
  /* Accent is a DARK green and phosphor is a bright one. They are never
     interchangeable: #66ff9e means the tube or a healthy link and nothing else,
     so a control can never be mistaken for something the clock is doing. */
  --acc:#2f7d5c; --on-acc:#eaf7f0; --val:#7fbf9c; --sel:#14261f; --open:#11201a;
  --card-line:#2f5c49; --card:#0e1b16; --card-cap:#6b8b7c;
  --phos:#66ff9e;
  --warn:#f0b429; --bad:#f0685f;
}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
/* Sora and IBM Plex Mono are the design's faces, but they are Google-hosted and
   this page must work on a network with no route out — the rule the top of this
   file sets. The handoff allows exactly this fallback and the design survives it. */
body{margin:0;background:var(--page);color:var(--text);
  font:14px/1.5 Sora,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.mono,code,td.v,.val,.chip,#trace,#scene,.fname{font-family:"IBM Plex Mono",ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}

/* ---- top bar ---- */
.top{display:flex;align-items:center;justify-content:space-between;gap:16px;
  height:58px;padding:0 20px;background:var(--chrome);border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:22px;min-width:0}
.wordmark{font-size:15px;font-weight:600;white-space:nowrap}
.tabs{display:flex;gap:4px;font-size:13px}
.tab{padding:6px 12px;border-radius:7px;color:var(--body);cursor:pointer;border:0;
  background:none;font:inherit;font-size:13px;white-space:nowrap}
.tab:hover{background:var(--hover)}
.tab.on{background:var(--sel);color:var(--val)}
.stat{display:flex;align-items:center;gap:9px;font-size:11px;color:var(--body);
  white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dot{width:7px;height:7px;border-radius:50%;background:var(--body);flex:none}
.dot.ok{background:var(--phos)}
.dot.warn{background:var(--warn)}
.dot.bad{background:var(--bad)}

/* ---- tab panels ---- */
.panel{display:none}
.panel.on{display:block}
.pad{padding:22px 28px 28px;max-width:1000px}

/* ---- faces: drawer + face page ---- */
.faces{display:grid;grid-template-columns:268px 1fr;min-height:calc(100vh - 58px)}
.drawer{border-right:1px solid var(--line);background:var(--furn);display:flex;
  flex-direction:column;min-width:0}
.filter{padding:12px 12px 8px}
.filter div{display:flex;align-items:center;gap:8px;height:34px;padding:0 11px;
  border:1px solid var(--ctl);border-radius:8px;background:var(--input)}
.filter span{font-size:12px;color:var(--cap)}
.filter input{flex:1;min-width:0;border:0;background:none;color:var(--text);
  font:inherit;font-size:13px;padding:0}
.filter input:focus{outline:none}
.filter input::placeholder{color:var(--cap)}
.fams{padding:4px 10px 16px;display:flex;flex-direction:column;gap:2px;
  overflow-y:auto}
.fam{display:flex;flex-direction:column}
.famhd{display:flex;align-items:center;justify-content:space-between;padding:9px 11px;
  border-radius:8px;cursor:pointer;font-size:11.5px;letter-spacing:.1em;color:var(--body)}
.famhd:hover{background:var(--hover)}
.fam.open .famhd{color:var(--val);background:var(--open)}
.famhd .n{font-size:10.5px;color:var(--count)}
.faceli{display:flex;flex-direction:column;gap:1px;padding:3px 0 8px 11px}
.frow{display:flex;align-items:center;gap:9px;padding:7px 10px;border-radius:7px;
  cursor:pointer;font-size:13px;color:var(--text);border:0;background:none;
  font-family:inherit;width:100%;text-align:left}
.frow:hover{background:var(--hover)}
.frow .d{width:4px;height:4px;border-radius:50%;background:#2f3a47;flex:none}
.frow.on{background:var(--sel);color:var(--val)}
.frow.on .d{background:var(--acc);box-shadow:0 0 6px var(--acc)}

.facepage{display:flex;flex-direction:column;min-width:0}
.fhead{padding:26px 28px;display:flex;gap:26px;border-bottom:1px solid var(--line);
  flex-wrap:wrap}
.disc{width:178px;height:178px;flex:none;border-radius:50%;border:1px solid #2b3a33;
  background:radial-gradient(circle at 50% 45%,rgba(102,255,158,.18),rgba(6,10,8,.92) 72%);
  position:relative;overflow:hidden;display:flex;align-items:center;justify-content:center}
/* Rings centred explicitly rather than by flex: they are absolutely positioned,
   so they are out of flow and the container's centring does not reach them. */
.disc i{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);
  width:33%;height:33%;border-radius:50%;border:1px solid rgba(102,255,158,.22)}
.disc i.o{width:66%;height:66%;border-color:rgba(102,255,158,.16)}
/* The sweep must state its own size. inset:0 cannot widen it, because an
   explicit width beats the right offset — which left it a 33%-wide band stuck
   to the left edge instead of a full-width scan. */
.disc .sw{left:0;top:0;transform:none;width:100%;height:100%;border:0;border-radius:0;
  background:linear-gradient(rgba(102,255,158,.12),transparent);animation:sweep 4s linear infinite}
.disc canvas{position:absolute;inset:0;width:100%;height:100%;display:block}
/* The sweep stands in for a live tube. Once a real preview has loaded it has
   nothing to stand in for, so it goes — as the handoff asks. */
.disc.live .sw{display:none}
@keyframes sweep{0%{transform:translateY(0);opacity:0}40%{opacity:.5}100%{transform:translateY(100%);opacity:0}}
@media(prefers-reduced-motion:reduce){.disc .sw{animation:none;opacity:.18}}
.fmeta{display:flex;flex-direction:column;gap:12px;padding-top:6px;min-width:0}
.fmrow{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.ffam{font-size:10px;letter-spacing:.2em;color:var(--cap)}
.pill{font-size:10px;letter-spacing:.16em;color:var(--phos);border:1px solid #1d5e42;
  border-radius:999px;padding:2px 8px;white-space:nowrap}
.pill.off{display:none}
.fname{font-size:30px;font-weight:600;letter-spacing:-.02em;word-break:break-word}
.fdesc{font-size:14px;line-height:1.6;color:var(--body);max-width:470px;text-wrap:pretty}

/* ---- settings ---- */
.slab{font-size:10px;letter-spacing:.18em;color:var(--cap)}
.fset{padding:22px 28px 24px;display:flex;flex-direction:column;gap:18px}
.g2{display:grid;grid-template-columns:1fr 1fr;gap:22px 36px}
@media(max-width:900px){.g2{grid-template-columns:1fr}}
.fld{display:flex;flex-direction:column;gap:7px;min-width:0}
.fldhd{display:flex;justify-content:space-between;align-items:center;gap:10px;font-size:13px}
.val{color:var(--val);font-size:12px}
.cap{font-size:11.5px;color:var(--cap);line-height:1.45}
.card{display:flex;flex-direction:column;gap:8px;padding:13px;border:1px solid var(--card-line);
  border-radius:10px;background:var(--card)}
.card .val{font-size:12.5px}
.card .cap{color:var(--card-cap)}
.card.off{display:none}

/* ---- tube strip ---- */
.strip{margin-top:auto;border-top:1px solid var(--line);background:var(--furn);
  padding:14px 28px;display:flex;align-items:center;gap:26px;flex-wrap:wrap}
.strip .slab{white-space:nowrap}
.sgrp{display:flex;align-items:center;gap:9px;font-size:12.5px;color:var(--body)}
.sgrp .val{font-size:11.5px}
.link{background:none;border:0;font:inherit;font-size:11.5px;color:var(--body);
  cursor:pointer;padding:0;font-family:"IBM Plex Mono",ui-monospace,monospace}
.link:hover{color:var(--text2)}
.tgl{width:28px;height:16px;border-radius:999px;background:#232b36;position:relative;
  border:0;padding:0;cursor:pointer;flex:none}
.tgl i{position:absolute;top:2px;left:2px;width:12px;height:12px;border-radius:50%;
  background:#5f6b7a;transition:left .12s,background .12s}
.tgl.on{background:#1d5e42}
.tgl.on i{left:14px;background:var(--phos)}
.pop{display:none;padding:14px 28px;border-top:1px solid var(--line);background:var(--furn)}
.pop.on{display:block}

/* ---- generic controls (shared with scene/notify/system) ---- */
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
button{font:inherit;font-size:13px;color:var(--text2);background:var(--input);
  border:1px solid var(--ctl);border-radius:8px;padding:8px 14px;cursor:pointer;
  transition:border-color .12s,background .12s,color .12s}
button:hover{border-color:#3b4756}
button:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
button.primary{background:var(--acc);border-color:var(--acc);color:var(--on-acc);font-weight:500}
button.primary:hover{background:#37916b;border-color:#37916b}
.tools button.on,button.on{border-color:var(--acc);color:var(--val);background:var(--sel)}
input[type=text],textarea,select{font:inherit;color:var(--text);background:var(--input);
  border:1px solid var(--ctl);border-radius:8px;padding:8px 11px;width:100%}
input[type=text]{flex:1;min-width:11rem}
input:focus,textarea:focus,select:focus{outline:none;border-color:var(--acc)}
textarea{font-size:13px;line-height:1.6;height:132px;resize:vertical;
  font-family:"IBM Plex Mono",ui-monospace,monospace}
::placeholder{color:var(--cap)}
input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:8rem;
  height:4px;padding:0;background:var(--ctl);border-radius:2px;accent-color:var(--acc)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:14px;
  border-radius:50%;background:var(--acc);border:0;cursor:pointer}
input[type=range]::-moz-range-thumb{width:14px;height:14px;border-radius:50%;
  background:var(--acc);border:0;cursor:pointer}
input[type=time]{font:inherit;font-size:12px;color:var(--text);background:var(--input);
  border:1px solid var(--ctl);border-radius:7px;padding:5px 9px;width:auto}
.mini{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--body)}
.mini select{width:auto;padding:5px 9px;font-size:12px;border-radius:7px}
.mini input[type=number]{width:3.6rem;padding:4px 6px;font-size:12px;background:var(--input);
  color:var(--text);border:1px solid var(--ctl);border-radius:6px;font:inherit;font-size:12px}
.sp{flex:1}
h2{display:flex;align-items:center;gap:8px;margin:0 0 14px;font-size:10px;font-weight:500;
  text-transform:uppercase;letter-spacing:.18em;color:var(--cap)}
svg.i{width:14px;height:14px;fill:currentColor;flex:none}
.hint{color:var(--cap);font-size:11.5px;line-height:1.55;margin:12px 0 0;max-width:70ch}
.hint b{color:var(--body);font-weight:500}
.hint a{color:var(--val);text-decoration:none}
.hint a:hover{text-decoration:underline}
section{margin:0 0 26px}
table{width:100%;border-collapse:collapse;font-size:13px;max-width:640px}
td{padding:8px 0;border-bottom:1px solid rgba(255,255,255,.045)}
tr:last-child td{border-bottom:0}
td:first-child{color:var(--body)}
td.v{text-align:right;font-size:12.5px;overflow-wrap:anywhere}
@media(max-width:560px){
  table td{display:block;border-bottom:0;padding:1px 0}
  table td:first-child{font-size:12px}
  table td.v{text-align:left;padding-bottom:9px;border-bottom:1px solid rgba(255,255,255,.045)}
  table tr:last-child td.v{border-bottom:0}
}
.warn{color:var(--warn)}
.bad{color:var(--bad)}
body.offline .top,body.offline .faces,body.offline .pad{opacity:.55;transition:opacity .2s}
.builder{display:grid;gap:12px;grid-template-columns:1fr}
@media(min-width:820px){.builder{grid-template-columns:minmax(0,1fr) minmax(0,1fr)}}
.tools{display:flex;flex-wrap:wrap;gap:5px;align-items:center;margin-bottom:9px}
.tools button{padding:5px 9px;font-size:12px}
#trace{font-size:12px;line-height:1.55;max-height:19rem;overflow-y:auto;
  background:var(--page);border:1px solid var(--line);border-radius:8px;padding:8px 10px}
#trace div{white-space:pre-wrap;word-break:break-word}
#trace .tx{color:var(--acc)}
#trace .rx{color:var(--phos)}
#trace .t{color:var(--cap)}
#trace .b{color:var(--cap);font-size:11px}
svg#cv{display:block;width:100%;height:auto;aspect-ratio:1;background:#0b0d0c;
  border:1px solid var(--line);border-radius:8px;touch-action:none;cursor:crosshair}
svg#cv.sel{cursor:default}
.meta{display:flex;justify-content:space-between;gap:10px;margin-top:7px;
  font-size:12px;color:var(--body);font-family:"IBM Plex Mono",ui-monospace,monospace}
.meta .over{color:var(--warn)}
footer{margin-top:20px;color:var(--cap);font-size:11.5px}
footer a{color:var(--val);text-decoration:none}
footer a:hover{text-decoration:underline}

/* ---- phone fold ---- */
@media(max-width:560px){
  .faces{grid-template-columns:1fr}
  .drawer{border-right:0;border-bottom:1px solid var(--line)}
  .famhd,.frow{min-height:44px}
  .fhead{padding:20px 18px}
  .disc{width:56vw;height:56vw;max-width:260px;max-height:260px;margin:0 auto}
  .fmeta{align-items:flex-start}
  .fset{padding:18px}
  .strip{padding:14px 18px;gap:16px}
  .pad{padding:18px}
  .top{padding:0 14px;gap:10px}
  .stat span.s{display:none}
  /* Two screens rather than two columns: the drawer is the landing screen and
     picking a face pushes the face page in. */
  body.faceview .drawer{display:none}
  body:not(.faceview) .facepage{display:none}
  .back{display:flex}
}
.back{display:none;align-items:center;gap:8px;padding:12px 18px 0;font-size:12px;
  color:var(--body);background:none;border:0;cursor:pointer;font-family:inherit}
</style></head><body>
<svg style="display:none"><symbol id="i-clock" viewBox="0 0 256 256"><path d="M128,24A104,104,0,1,0,232,128,104.11,104.11,0,0,0,128,24Zm0,192a88,88,0,1,1,88-88A88.1,88.1,0,0,1,128,216Zm64-88a8,8,0,0,1-8,8H128a8,8,0,0,1-8-8V72a8,8,0,0,1,16,0v48h48A8,8,0,0,1,192,128Z"/></symbol><symbol id="i-cube" viewBox="0 0 256 256"><path d="M223.68,66.15,135.68,18h0a15.88,15.88,0,0,0-15.36,0l-88,48.17a16,16,0,0,0-8.32,14v95.64a16,16,0,0,0,8.32,14l88,48.17a15.88,15.88,0,0,0,15.36,0l88-48.17a16,16,0,0,0,8.32-14V80.18A16,16,0,0,0,223.68,66.15ZM128,32h0l80.34,44L128,120,47.66,76ZM40,90l80,43.78v85.79L40,175.82Zm96,129.57V133.82L216,90v85.78Z"/></symbol><symbol id="i-gear" viewBox="0 0 256 256"><path d="M128,80a48,48,0,1,0,48,48A48.05,48.05,0,0,0,128,80Zm0,80a32,32,0,1,1,32-32A32,32,0,0,1,128,160Zm88-29.84q.06-2.16,0-4.32l14.92-18.64a8,8,0,0,0,1.48-7.06,107.21,107.21,0,0,0-10.88-26.25,8,8,0,0,0-6-3.93l-23.72-2.64q-1.48-1.56-3-3L186,40.54a8,8,0,0,0-3.94-6,107.71,107.71,0,0,0-26.25-10.87,8,8,0,0,0-7.06,1.49L130.16,40Q128,40,125.84,40L107.2,25.11a8,8,0,0,0-7.06-1.48A107.6,107.6,0,0,0,73.89,34.51a8,8,0,0,0-3.93,6L67.32,64.27q-1.56,1.49-3,3L40.54,70a8,8,0,0,0-6,3.94,107.71,107.71,0,0,0-10.87,26.25,8,8,0,0,0,1.49,7.06L40,125.84Q40,128,40,130.16L25.11,148.8a8,8,0,0,0-1.48,7.06,107.21,107.21,0,0,0,10.88,26.25,8,8,0,0,0,6,3.93l23.72,2.64q1.49,1.56,3,3L70,215.46a8,8,0,0,0,3.94,6,107.71,107.71,0,0,0,26.25,10.87,8,8,0,0,0,7.06-1.49L125.84,216q2.16.06,4.32,0l18.64,14.92a8,8,0,0,0,7.06,1.48,107.21,107.21,0,0,0,26.25-10.88,8,8,0,0,0,3.93-6l2.64-23.72q1.56-1.48,3-3L215.46,186a8,8,0,0,0,6-3.94,107.71,107.71,0,0,0,10.87-26.25,8,8,0,0,0-1.49-7.06Zm-16.1-6.5a73.93,73.93,0,0,1,0,8.68,8,8,0,0,0,1.74,5.48l14.19,17.73a91.57,91.57,0,0,1-6.23,15L187,173.11a8,8,0,0,0-5.1,2.64,74.11,74.11,0,0,1-6.14,6.14,8,8,0,0,0-2.64,5.1l-2.51,22.58a91.32,91.32,0,0,1-15,6.23l-17.74-14.19a8,8,0,0,0-5-1.75h-.48a73.93,73.93,0,0,1-8.68,0,8,8,0,0,0-5.48,1.74L100.45,215.8a91.57,91.57,0,0,1-15-6.23L82.89,187a8,8,0,0,0-2.64-5.1,74.11,74.11,0,0,1-6.14-6.14,8,8,0,0,0-5.1-2.64L46.43,170.6a91.32,91.32,0,0,1-6.23-15l14.19-17.74a8,8,0,0,0,1.74-5.48,73.93,73.93,0,0,1,0-8.68,8,8,0,0,0-1.74-5.48L40.2,100.45a91.57,91.57,0,0,1,6.23-15L69,82.89a8,8,0,0,0,5.1-2.64,74.11,74.11,0,0,1,6.14-6.14A8,8,0,0,0,82.89,69L85.4,46.43a91.32,91.32,0,0,1,15-6.23l17.74,14.19a8,8,0,0,0,5.48,1.74,73.93,73.93,0,0,1,8.68,0,8,8,0,0,0,5.48-1.74L155.55,40.2a91.57,91.57,0,0,1,15,6.23L173.11,69a8,8,0,0,0,2.64,5.1,74.11,74.11,0,0,1,6.14,6.14,8,8,0,0,0,5.1,2.64l22.58,2.51a91.32,91.32,0,0,1,6.23,15l-14.19,17.74A8,8,0,0,0,199.87,123.66Z"/></symbol><symbol id="i-image" viewBox="0 0 256 256"><path d="M216,40H40A16,16,0,0,0,24,56V200a16,16,0,0,0,16,16H216a16,16,0,0,0,16-16V56A16,16,0,0,0,216,40Zm0,16V158.75l-26.07-26.06a16,16,0,0,0-22.63,0l-20,20-44-44a16,16,0,0,0-22.62,0L40,149.37V56ZM40,172l52-52,80,80H40Zm176,28H194.63l-36-36,20-20L216,181.38V200ZM144,100a12,12,0,1,1,12,12A12,12,0,0,1,144,100Z"/></symbol><symbol id="i-megaphone-simple" viewBox="0 0 256 256"><path d="M228.54,86.66l-176.06-54A16,16,0,0,0,32,48V192a16,16,0,0,0,16,16,16,16,0,0,0,4.52-.65L136,181.73V192a16,16,0,0,0,16,16h32a16,16,0,0,0,16-16v-29.9l28.54-8.75A16.09,16.09,0,0,0,240,138V102A16.09,16.09,0,0,0,228.54,86.66ZM136,165,48,192V48l88,27Zm48,27H152V176.82L184,167Zm40-54-.11,0L152,160.08V79.92l71.89,22,.11,0v36Z"/></symbol><symbol id="i-pulse" viewBox="0 0 256 256"><path d="M240,128a8,8,0,0,1-8,8H204.94l-37.78,75.58A8,8,0,0,1,160,216h-.4a8,8,0,0,1-7.08-5.14L95.35,60.76,63.28,131.31A8,8,0,0,1,56,136H24a8,8,0,0,1,0-16H50.85L88.72,36.69a8,8,0,0,1,14.76.46l57.51,151,31.85-63.71A8,8,0,0,1,200,120h32A8,8,0,0,1,240,128Z"/></symbol><symbol id="i-sun" viewBox="0 0 256 256"><path d="M120,40V16a8,8,0,0,1,16,0V40a8,8,0,0,1-16,0Zm72,88a64,64,0,1,1-64-64A64.07,64.07,0,0,1,192,128Zm-16,0a48,48,0,1,0-48,48A48.05,48.05,0,0,0,176,128ZM58.34,69.66A8,8,0,0,0,69.66,58.34l-16-16A8,8,0,0,0,42.34,53.66Zm0,116.68-16,16a8,8,0,0,0,11.32,11.32l16-16a8,8,0,0,0-11.32-11.32ZM192,72a8,8,0,0,0,5.66-2.34l16-16a8,8,0,0,0-11.32-11.32l-16,16A8,8,0,0,0,192,72Zm5.66,114.34a8,8,0,0,0-11.32,11.32l16,16a8,8,0,0,0,11.32-11.32ZM48,128a8,8,0,0,0-8-8H16a8,8,0,0,0,0,16H40A8,8,0,0,0,48,128Zm80,80a8,8,0,0,0-8,8v24a8,8,0,0,0,16,0V216A8,8,0,0,0,128,208Zm112-88H216a8,8,0,0,0,0,16h24a8,8,0,0,0,0-16Z"/></symbol><symbol id="i-wifi-high" viewBox="0 0 256 256"><path d="M140,204a12,12,0,1,1-12-12A12,12,0,0,1,140,204ZM237.08,87A172,172,0,0,0,18.92,87,8,8,0,0,0,29.08,99.37a156,156,0,0,1,197.84,0A8,8,0,0,0,237.08,87ZM205,122.77a124,124,0,0,0-153.94,0A8,8,0,0,0,61,135.31a108,108,0,0,1,134.06,0,8,8,0,0,0,11.24-1.3A8,8,0,0,0,205,122.77Zm-32.26,35.76a76.05,76.05,0,0,0-89.42,0,8,8,0,0,0,9.42,12.94,60,60,0,0,1,70.58,0,8,8,0,1,0,9.42-12.94Z"/></symbol></svg>

<div class="top">
  <div class="brand">
    <div class="wordmark">Scope Clock</div>
    <div class="tabs">
      <button class="tab on" data-t="faces">Faces</button>
      <button class="tab" data-t="scene">Scene</button>
      <button class="tab" data-t="notify">Notify</button>
      <button class="tab" data-t="system">System</button>
    </div>
  </div>
  <div class="stat"><span class="dot" id="dot"></span><span id="sub">connecting&hellip;</span></div>
</div>

<div class="panel on" id="p-faces">
 <div class="faces">
  <div class="drawer">
    <div class="filter">
      <div><span class="mono">/</span><input type="text" id="ffilter" placeholder="Filter faces" autocomplete="off"></div>
    </div>
    <div class="fams" id="fams"></div>
  </div>

  <div class="facepage">
    <button class="back" id="back">&larr; <span id="backfam">faces</span></button>
    <div class="fhead">
      <div class="disc" id="disc"><i></i><i class="o"></i><canvas id="pv" width="356" height="356"></canvas><i class="sw"></i></div>
      <div class="fmeta">
        <div class="fmrow">
          <div class="ffam mono" id="ffam">&nbsp;</div>
          <div class="pill off mono" id="fpill">ON THE TUBE</div>
        </div>
        <div class="fname" id="ftitle">&nbsp;</div>
        <div class="fdesc" id="fdesc">&nbsp;</div>
      </div>
    </div>

    <div class="fset">
      <div class="slab mono">SETTINGS FOR THIS FACE</div>
      <div class="g2">
        <div class="fld">
          <div class="fldhd"><span>Size on the tube</span><span class="val mono" id="fscaleval">--</span></div>
          <input type="range" id="fscale" min="20" max="250" step="5">
          <div class="cap">Remembered for this face alone. Below ~30% the labels stop
            shrinking with the drawing &mdash; the font's scale is a whole number and
            bottoms out. Also settable at the clock: hold the knob's button, turn to
            adjust, tap to leave.</div>
        </div>
        <div class="fld">
          <div class="fldhd"><span>Typeface</span><select id="font"></select></div>
          <div class="cap">Faces that ask for their own keep it &mdash; the digital clock
            stays seven-segment whatever you pick. Also an entity in Home Assistant.</div>
        </div>

        <div class="card off" id="c-elem">
          <div class="fldhd"><span>Element</span><select id="elem"></select></div>
          <div class="cap">Only the atom face has this. Left alone it walks through the
            118 on its own.</div>
        </div>
        <div class="card off" id="c-con">
          <div class="fldhd"><span>Constellation</span><select id="con"></select></div>
          <div class="cap">One of the 88, or let it cycle. An entity in Home Assistant
            either way.</div>
        </div>
        <div class="card off" id="c-np">
          <div class="fldhd"><span>Show when music starts</span>
            <button class="tgl" id="autonp" aria-label="now playing takeover"><i></i></button></div>
          <div class="cap">Appears when a track starts or changes, never mid-song. Pick
            another face while it is playing and that choice stays until the music stops.</div>
        </div>
      </div>
    </div>

    <div class="strip">
      <div class="slab mono">TUBE &middot; ALL FACES</div>
      <div class="sgrp" style="min-width:190px;flex:1;max-width:280px">
        <span>bright</span><input type="range" id="bri" min="0" max="255" step="5">
        <span class="val mono" id="brival">--</span>
      </div>
      <div class="sgrp"><button class="tgl" id="wobble" aria-label="anti burn-in drift"><i></i></button> drift</div>
      <div class="sgrp"><button class="tgl" id="sleep" aria-label="sleep"><i></i></button> sleep</div>
      <button class="link" id="pop-c-btn">centre <span id="alsum">0,0</span> &#9656;</button>
      <button class="link" id="pop-t-btn">target rings &#9656;</button>
      <button class="link" id="pop-s-btn">sleep schedule &#9656;</button>
    </div>

    <div class="pop" id="pop-c">
      <div class="row"><label class="mini" style="flex:1;gap:9px;max-width:420px">centre X
        <input type="range" id="alx" min="-600" max="600" step="5">
        <span class="val mono" id="alxv" style="min-width:3.4rem;text-align:right">--</span></label></div>
      <div class="row" style="margin-top:6px"><label class="mini" style="flex:1;gap:9px;max-width:420px">centre Y
        <input type="range" id="aly" min="-600" max="600" step="5">
        <span class="val mono" id="alyv" style="min-width:3.4rem;text-align:right">--</span></label></div>
      <div class="row" style="margin-top:8px">
        <button id="alnudgexl">&larr;</button><button id="alnudgexr">&rarr;</button>
        <button id="alnudgeyd">&darr;</button><button id="alnudgeyu">&uarr;</button>
        <button id="alzero">centre</button>
      </div>
      <p class="hint">Shifts the whole image, in DAC counts, on top of the trimmer pots
        inside the case &mdash; so the pots still work and this is the fine adjustment.</p>
    </div>

    <div class="pop" id="pop-t">
      <div class="row"><button id="altarget" class="primary">Show target rings</button></div>
      <p class="hint">Concentric rings at 1/3, 2/3 and the full working radius: the outer
        ring should sit exactly on the glass, and the cardinal ticks tell you which way it
        has moved if it does not. That face is never resized by the size slider and holds
        the anti burn-in drift still while it is up, because a reference that wanders is
        not a reference.</p>
    </div>

    <div class="pop" id="pop-s">
      <div class="row">
        <input type="time" id="slpfrom"><span class="mini">to</span><input type="time" id="slpto">
        <button id="slpsave">Apply</button><button id="slpoff">No schedule</button>
        <span class="mini mono" id="slpwin">--</span>
      </div>
      <p class="hint">Warm standby: the beam is blanked so the phosphor takes nothing, but
        the tube stays lit and waking is the next frame. There is no deeper state &mdash;
        this board has one tube control, the blanking input, and no heater or HV switch, so
        a real power-down would need a MOSFET fitting. Leaving it warm is kinder to the tube
        than it sounds: a CRT is worn by heater hours and by thermal cycling both.<br>
        The window wraps, so 23:00 to 07:00 is the one you want. A manual override holds
        until the next edge. Any touch of the knob wakes it, and that first touch is
        swallowed so it does not also change the face.</p>
    </div>
  </div>
 </div>
</div>

<div class="panel" id="p-scene"><div class="pad">
<section class="span2">
  <h2><svg class="i"><use href="#i-image"/></svg>Scene</h2>
  <div class="builder">
    <div>
      <div class="tools">
        <button class="t on" data-tool="sel">Select</button>
        <button class="t" data-tool="L">Line</button>
        <button class="t" data-tool="C">Circle</button>
        <button class="t" data-tool="T">Text</button>
        <button class="t" data-tool="D">Clock</button>
        <button class="t" data-tool="H">Hand</button>
        <span class="sp"></span>
        <label class="mini">scale<input type="number" id="tscale" value="10" min="1" max="60"></label>
        <label class="mini">hand<select id="hsrc"><option value="0">sec</option><option value="1">min</option><option value="2">hour</option></select></label>
        <label class="mini"><input type="checkbox" id="snap" checked>snap</label>
        <button id="b-del">Delete</button>
        <button id="b-wipe">Clear all</button>
        <button id="b-live">Live</button>
        <button id="b-svg">Import SVG</button>
        <input type="file" id="svgfile" accept=".svg,image/svg+xml" style="display:none">
      </div>
      <svg id="cv" viewBox="-1330 -1330 2660 2660"></svg>
      <div class="meta"><span id="xy">&mdash;</span><span id="livestat"></span><span id="cnt">0 items</span></div>
    </div>
    <textarea id="scene" spellcheck="false" placeholder="C 0 0 900
L -600 -600 600 600
D -430 -1215 9 %H:%M:%S"></textarea>
  </div>
  <div class="row" style="margin-top:10px">
    <button class="primary" id="b-push">Push</button>
    <button id="b-clear">Clear</button>
  </div>
  <p class="hint">Draw on the left or type on the right — they are the same scene,
    kept in step. The dashed ring is the rim of the tube at &plusmn;1200 device
    units; the solid one is where the DAC itself runs out. Drag to make lines, circles and
    hands; click to place text. With <b>Select</b>, drag an item to move it or
    grab a white handle to reshape it — line ends, circle and hand radii, and the
    right edge of a text box sets its scale. <b>Live</b> mirrors every edit
    straight onto the tube. <b>Import SVG</b> flattens a drawing to lines in the
    browser and fits it to the tube, simplifying until it fits the device's
    192-item list.<br>
    <code>L</code> line · <code>C</code> circle · <code>T</code> text ·
    <code>D</code> live clock text · <code>H</code> hand (sec/min/hour).
    <code>D</code> and <code>H</code> make it a face template: the device re-renders
    them from its own RTC, so it keeps telling the time with the bridge unplugged.
    Use <code>tools/vec2scene.py</code> to turn artwork into one of these.</p>
</section>
</div></div>

<div class="panel" id="p-notify"><div class="pad">
<section>
  <h2><svg class="i"><use href="#i-megaphone-simple"/></svg>Notification</h2>
  <div class="row" style="margin-bottom:8px">
    <input type="text" id="ntitle" placeholder="title (optional)" maxlength="31">
  </div>
  <div class="row">
    <input type="text" id="bmsg" placeholder="message" maxlength="60">
    <button class="primary" id="b-send">Send</button>
  </div>
  <div class="row" style="margin-top:8px">
    <input type="text" id="tick" placeholder="ticker text" maxlength="150">
    <button id="b-tick">Scroll</button>
  </div>
  <div class="row" style="margin-top:8px">
    <label class="mini">where
      <select id="nplace">
        <option value="bottom">bottom strip</option>
        <option value="top">top strip</option>
        <option value="center">centred card</option>
      </select></label>
    <label class="mini">for
      <input type="number" id="nms" value="8" min="1" max="60" step="1">s</label>
    <label class="mini"><input type="checkbox" id="nsolo">blank behind</label>
    <span class="sp"></span>
    <button id="b-nclear">Clear</button>
  </div>
  <p class="hint">Overlaid on whatever is showing, and it expires on the
    <em>device</em> — a bridge that dies cannot strand one on screen. A strip is a
    single line and shrinks to fit; the centred card keeps the title on its own
    line and draws a frame so it reads over a busy face. <b>Blank behind</b> drops
    the face entirely for the duration, which is what you want for a card. Also on MQTT at
    <code>notify/set</code>, and in Home Assistant as a notify entity.</p>
</section>
</div></div>

<div class="panel" id="p-system"><div class="pad">
<section class="span2">
  <h2><svg class="i"><use href="#i-pulse"/></svg>Status</h2>
  <table>
    <tr><td>Mode</td><td class="v" id="s-mode">--</td></tr>
    <tr><td>Refresh</td><td class="v" id="s-frame">--</td></tr>
    <tr><td>RTC</td><td class="v" id="s-rtc">--</td></tr>
    <tr><td>Last time sync</td><td class="v" id="s-sync">--</td></tr>
    <tr><td>Device uptime</td><td class="v" id="s-up">--</td></tr>
    <tr><td>MQTT</td><td class="v" id="s-mqtt">--</td></tr>
    <tr><td>Wi-Fi</td><td class="v" id="s-wifi">--</td></tr>
    <tr><td>Device last heard us</td><td class="v" id="s-silent">--</td></tr>
  </table>
  <div class="row" style="margin-top:14px">
    <button id="b-relink">Relink</button>
  </div>
  <p class="hint">The link fails one way only: the device stops hearing us while we
    still hear it. The signature is <em>last heard us</em> climbing while everything
    else looks healthy. Relink resets the bridge's USB peripheral and the device
    restarts itself, which recovers it without touching the clock — give it a
    couple of minutes.</p>
</section>
<section class="span2">
  <h2><svg class="i"><use href="#i-pulse"/></svg>Link trace</h2>
  <div class="row" style="margin-bottom:8px">
    <button id="tr-pause">pause</button>
    <button id="tr-clear">clear</button>
    <label class="mini"><input type="checkbox" id="tr-hb" checked>hide Status heartbeat</label>
    <span class="sp"></span>
    <label class="mini"><input type="checkbox" id="tr-hex">show bytes</label>
  </div>
  <div id="trace"></div>
  <p class="hint">Every frame in both directions, newest at the bottom.
    <b>&rarr;</b> is the bridge talking to the clock, <b>&larr;</b> the clock
    talking back. Both flowing means the link is healthy; only <b>&larr;</b>
    moving is the one-way failure described above, and you will see it here
    before any symptom reaches the tube. The ring holds the last 96 frames and
    keeps the first 24 bytes of each.</p>
</section>
<footer><svg class="i" style="vertical-align:-2px"><use href="#i-gear"/></svg>
  <a href="/config">Network &amp; MQTT settings</a> ·
  icons by <a href="https://phosphoricons.com">Phosphor</a> (MIT)</footer>
</div></div>

<script>
var FACES=[], FAMS=[], FAMOF={}, openFam="", selFace="", curFace="", filt="";
function el(i){return document.getElementById(i)}
function post(p,b){return fetch(p,{method:"POST",body:b}).then(function(){setTimeout(poll,400)})}
function dur(s){s=+s;if(!isFinite(s))return"--";
  if(s<60)return s+"s";if(s<3600)return Math.floor(s/60)+"m "+(s%60)+"s";
  return Math.floor(s/3600)+"h "+Math.floor(s%3600/60)+"m"}
function setDot(cls,txt){el("dot").className="dot "+cls;el("sub").textContent=txt}
function esc(t){return String(t).replace(/[&<>"]/g,function(c){
  return {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]})}

// One line per face, in the device's own order. A face missing from here still
// works — it just gets the fallback — so adding a face to the firmware never
// breaks this page.
var DESC={
hands:"Hands on a dial with Roman numerals. The default face.",
numbers:"The same dial, numbered rather than lettered.",
tickdial:"Bare ticks and hands, no numerals at all.",
orbit:"Hours and minutes as bodies on rings rather than hands.",
sector:"Time as filled sectors sweeping round the dial.",
digital:"Seven-segment numerals. Keeps its own typeface whatever you pick globally.",
datetime:"The time with the date underneath it.",
wordclock:"The time spelled out in words.",
binary:"The time in BCD, a column of bits per digit.",
tetra:"Tetrahedron in wireframe, rotating. The sparsest of the five solids.",
cube:"Wireframe cube, rotating. Sized to the tube and remembered at that size.",
octa:"Octahedron, rotating.",
icosa:"Twenty faces, rotating slowly. One of the sparser solids, so it takes a larger size well.",
dodeca:"Twelve pentagons. The densest of the Platonics, and the one that wants sizing down.",
tesseract:"A four-dimensional cube turning through three of them.",
torus:"A wireframe torus, rotating.",
lissajous:"Two sine waves against each other. The classic thing to point a scope at.",
harmonograph:"Damped pendulums drawing over one another.",
spirograph:"A circle rolling inside a circle, tracing as it goes.",
rose:"A rose curve: petals straight out of one polar equation.",
lorenz:"The Lorenz attractor, traced continuously.",
starpoly:"A star polygon, stepping through the forms {n/k} allows.",
starfield:"Stars rushing straight at you.",
tunnel:"A tunnel receding to a vanishing point.",
midiscope:"Live USB-MIDI as an X-Y figure. The ratio between two notes is the shape, so a fifth really does draw 3:2.",
midichord:"Live USB-MIDI as a chord wheel.",
matrix:"Digital rain in katakana, each column given the chord the round tube actually allows.",
nowplaying:"Track and artist, raised when music starts and dropped when it stops.",
gauges:"A few labelled percentages as concentric arcs. Deliberately generic, so any source can drive it.",
teapot:"The Utah teapot in wireframe, turning.",
sphere:"A wireframe sphere.",
knot:"A torus knot, rotating.",
mobius:"A Mobius band, turning.",
helix:"A helix rotating on its axis.",
atom:"Shell diagram for one of the 118. Left alone the element walks on its own.",
solar:"The inner planets on their orbits.",
moon:"Current phase, drawn as a terminator across the disc.",
weather:"Conditions and temperature, pushed down from the host.",
pong:"Pong, playing itself.",
life:"Conway's Life, seeded and left to run.",
trailclock:"Hands that leave a decaying trail behind them.",
ticker:"Marquee text, scrolling. Whatever the host last sent.",
worldclock:"Other cities as offsets from local time. The device never learns what a timezone is.",
asteroids:"Asteroids, playing itself.",
constell:"One of the 88 constellations, drawn with its stars and lines. Cycles unless you pin one.",
starglobe:"The celestial sphere turning, from the real catalogue.",
align:"The centring target: rings at a third, two thirds and the full working radius. Never resized, and it holds the drift still while it is up.",
radar:"Devices on the network as returns on a sweep, placed by how long they take to answer."
};

// ---- face preview --------------------------------------------------------
// Frames are baked from the real face code at build time (tools/hostsim/thumbs.cpp)
// and fetched per face, so the page stays small and only the face being looked
// at costs a request. Everything arrives as polylines — text and circles were
// already flattened to beam strokes — so this needs no glyph table.
var pvFrames=null, pvI=0, pvTimer=null, pvFace=-1;
function pvStop(){ if(pvTimer){clearInterval(pvTimer);pvTimer=null} }
function pvDecode(v){
  var o=0, nf=v.getUint8(o++), out=[];
  for(var f=0;f<nf;f++){
    var ns=v.getUint8(o++), st=[];
    for(var s=0;s<ns;s++){
      var np=v.getUint8(o++), p=new Int8Array(np*2);
      for(var k=0;k<np*2;k++) p[k]=v.getInt8(o++);
      st.push(p);
    }
    out.push(st);
  }
  return out;
}
function pvDraw(){
  var c=el("pv"); if(!c||!c.getContext) return;
  var g=c.getContext("2d"), W=c.width, H=c.height;
  g.clearRect(0,0,W,H);
  el("disc").classList.toggle("live", !!(pvFrames&&pvFrames.length));
  if(!pvFrames||!pvFrames.length) return;
  // Coordinates are DAC/16 and the tube's rim is 1800 counts, so 112.5 IS the
  // glass. Mapping that to the edge of the disc makes the disc mean the tube:
  // a face that overruns the field overruns the disc too and gets clipped by
  // the round mask, exactly as it would in real life. (gauges does, at the
  // authored size — see the fitScale note in CLAUDE.md.)
  var k=(W/2)/112.5, fr=pvFrames[pvI];
  g.save(); g.translate(W/2,H/2);
  g.strokeStyle="#66ff9e"; g.lineWidth=1.7; g.lineCap="round"; g.lineJoin="round";
  g.shadowColor="rgba(102,255,158,.5)"; g.shadowBlur=7;
  for(var i=0;i<fr.length;i++){
    var p=fr[i], n=p.length/2;
    g.beginPath();
    // Device Y counts up, canvas counts down.
    g.moveTo(p[0]*k, -p[1]*k);
    for(var j=1;j<n;j++) g.lineTo(p[j*2]*k, -p[j*2+1]*k);
    g.stroke();
  }
  g.restore();
}
function pvLoad(idx){
  if(idx<0||idx===pvFace) return;
  pvFace=idx; pvStop(); pvFrames=null; pvDraw();
  fetch("/api/thumb?i="+idx).then(function(r){
    if(!r.ok) throw 0; return r.arrayBuffer();
  }).then(function(b){
    if(pvFace!==idx) return;            // selection moved on while this was in flight
    pvFrames=pvDecode(new DataView(b)); pvI=0; pvDraw();
    // 180ms matches the step the frames were sampled at, so playback runs at
    // the speed the face actually animates.
    if(pvFrames.length>1) pvTimer=setInterval(function(){
      pvI=(pvI+1)%pvFrames.length; pvDraw(); },180);
  }).catch(function(){ pvFrames=null; pvDraw() });
}

function descOf(n){return DESC[n]||"One of the faces the knob walks past. Sized and centred like the rest."}

// The drawer is built from the device's own list, so a face added to the
// firmware appears here without this page being touched.
fetch("/api/faces").then(function(r){return r.json()}).then(function(a){
  FACES=a.map(function(e){return e.n});
  var by={},order=[];
  a.forEach(function(e){
    if(!by[e.g]){by[e.g]=[];order.push(e.g)}
    by[e.g].push(e.n); FAMOF[e.n]=e.g;
  });
  FAMS=order.map(function(g){return {g:g,faces:by[g]}});
  el("ffilter").placeholder="Filter "+FACES.length+" faces";
  // poll() runs on its own interval and can land BEFORE this fetch resolves, so
  // curFace may already be known. Prefer it: opening the drawer on the first
  // family while the tube is showing something else is exactly the disagreement
  // between page and device the handoff says to avoid.
  if(!selFace)selFace=(curFace&&FAMOF[curFace])?curFace:FAMS[0].faces[0];
  if(!openFam)openFam=FAMOF[selFace]||FAMS[0].g;
  drawDrawer(); showFace();
  poll();
});

function matches(n){return !filt||n.indexOf(filt)>=0}

function drawDrawer(){
  var h="";
  FAMS.forEach(function(f){
    var hit=f.faces.filter(matches);
    // A family with nothing matching hides entirely; the open one stays open
    // while typing, which is what stops the list jumping under the cursor.
    if(filt&&!hit.length)return;
    var open=(f.g===openFam);
    h+='<div class="fam'+(open?" open":"")+'" data-g="'+esc(f.g)+'">'+
       '<div class="famhd mono"><span>'+esc(f.g.toUpperCase())+'</span>'+
       '<span class="n">'+f.faces.length+'</span></div>';
    if(open||filt){
      h+='<div class="faceli">';
      (filt?hit:f.faces).forEach(function(n){
        h+='<button class="frow'+(n===selFace?" on":"")+'" data-f="'+esc(n)+'">'+
           '<span class="d"></span><span class="mono">'+esc(n)+'</span></button>';
      });
      h+='</div>';
    }
    h+='</div>';
  });
  el("fams").innerHTML=h||'<div class="cap" style="padding:10px 11px">no face matches</div>';
}

// Selecting pushes straight to the tube. The handoff left this open; on a clock
// you are looking at, the point of picking a face is to see it, and the knob has
// always worked that way — so the "Show on tube" button would only be a second
// step between you and the thing you already asked for. The pill stays as the
// confirmation, which is what the handoff says to keep if selection pushes.
function pick(n){ selFace=n; openFam=FAMOF[n]||openFam; drawDrawer(); showFace();
  document.body.classList.add("faceview"); post("/api/face",n); }

function showFace(){
  var fam=FAMOF[selFace]||"";
  el("ffam").textContent=fam.toUpperCase();
  el("backfam").textContent=fam.toLowerCase()||"faces";
  el("ftitle").textContent=selFace;
  el("fdesc").textContent=descOf(selFace);
  el("fpill").className="pill mono"+(selFace===curFace?"":" off");
  // Per-face cards: a setting that belongs to one face lives on that face.
  el("c-elem").className="card"+(selFace==="atom"?"":" off");
  el("c-con").className="card"+(selFace==="constell"?"":" off");
  el("c-np").className="card"+(selFace==="nowplaying"?"":" off");
  pvLoad(FACES.indexOf(selFace));
}

el("fams").addEventListener("click",function(e){
  var f=e.target.closest("[data-f]"); if(f){pick(f.dataset.f);return}
  var g=e.target.closest("[data-g]");
  // Opening a family selects its first face, per the handoff.
  if(g){var fam=FAMS.filter(function(x){return x.g===g.dataset.g})[0];
    if(fam){openFam=fam.g; selFace=fam.faces[0]; drawDrawer(); showFace(); post("/api/face",selFace)}}
});
el("ffilter").oninput=function(){filt=this.value.trim().toLowerCase();drawDrawer()};
el("back").onclick=function(){document.body.classList.remove("faceview")};

// Tabs
document.querySelectorAll(".tab").forEach(function(t){
  t.onclick=function(){
    document.querySelectorAll(".tab").forEach(function(x){x.classList.remove("on")});
    document.querySelectorAll(".panel").forEach(function(x){x.classList.remove("on")});
    t.classList.add("on"); el("p-"+t.dataset.t).classList.add("on");
  };
});
// Strip popovers, one at a time.
[["pop-c-btn","pop-c"],["pop-t-btn","pop-t"],["pop-s-btn","pop-s"]].forEach(function(pr){
  el(pr[0]).onclick=function(){
    var was=el(pr[1]).classList.contains("on");
    ["pop-c","pop-t","pop-s"].forEach(function(x){el(x).classList.remove("on")});
    if(!was)el(pr[1]).classList.add("on");
  };
});
// The three switches are buttons, not checkboxes, so they can carry the design's
// track-and-knob treatment. aria-pressed keeps them honest to a screen reader.
function tgl(id,url){
  el(id).onclick=function(){
    var on=!this.classList.contains("on");
    this.classList.toggle("on",on); this.setAttribute("aria-pressed",on?"true":"false");
    post(url,on?"1":"0");
  };
}
function setTgl(id,on){el(id).classList.toggle("on",!!on);
  el(id).setAttribute("aria-pressed",on?"true":"false")}
tgl("autonp","/api/autonp"); tgl("wobble","/api/wobble"); tgl("sleep","/api/sleep");

function jstr(x){return JSON.stringify(String(x))}
el("b-send").onclick=function(){
  var m=el("bmsg").value.trim(), t=el("ntitle").value.trim();
  if(!m&&!t)return;
  post("/api/notify","{\"title\":"+jstr(t)+",\"message\":"+jstr(m)+
    ",\"place\":"+jstr(el("nplace").value)+
    ",\"solo\":"+(el("nsolo").checked?"true":"false")+
    ",\"ms\":"+(Math.max(1,Math.min(60,+el("nms").value||8))*1000)+"}");
};
el("b-nclear").onclick=function(){post("/api/notify","{\"message\":\"\",\"ms\":0}")};
el("b-tick").onclick=function(){post("/api/ticker",el("tick").value)};
el("b-push").onclick=function(){post("/api/scene",el("scene").value)};
el("b-clear").onclick=function(){post("/api/scene","")};

/* ---- SVG import ---------------------------------------------------------
   Done in the browser, not the firmware, and not because it is easier: the page
   already has a complete SVG engine. getPointAtLength flattens beziers, arcs and
   every nested transform exactly, which an ESP32 parsing path data by hand would
   get wrong in a dozen ways. The device only ever sees straight lines.

   The item cap is the real constraint — 192 lines for the whole scene — so the
   flattened outlines are simplified with Douglas-Peucker, and the tolerance is
   searched for rather than guessed, since the right value depends entirely on
   the drawing. */
function svgPolylines(text){
  var doc = new DOMParser().parseFromString(text, "image/svg+xml");
  var svg = doc.documentElement;
  if(!svg || svg.tagName.toLowerCase()!=="svg") throw new Error("not an SVG");
  // Measuring needs the element laid out, and a fixed viewport makes getCTM
  // resolve the viewBox to a consistent space whatever the file declares.
  var host=document.createElement("div");
  host.style.cssText="position:absolute;left:-99999px;top:0;overflow:hidden";
  document.body.appendChild(host);
  svg.setAttribute("width","1000"); svg.setAttribute("height","1000");
  host.appendChild(svg);
  var out=[];
  try{
    var els=svg.querySelectorAll("path,line,polyline,polygon,rect,circle,ellipse");
    for(var i=0;i<els.length;i++){
      var el=els[i];
      if(typeof el.getTotalLength!=="function") continue;
      var len=0; try{ len=el.getTotalLength(); }catch(e){ continue; }
      if(!(len>0)) continue;
      // Two user units a sample: finer than the beam resolves once the drawing
      // is fitted to the tube, and the simplifier removes what is redundant.
      var n=Math.max(2,Math.min(600,Math.round(len/2)));
      var m=el.getCTM(), pts=[];
      for(var k=0;k<=n;k++){
        var p=el.getPointAtLength(len*k/n);
        pts.push(m?{x:m.a*p.x+m.c*p.y+m.e, y:m.b*p.x+m.d*p.y+m.f}:{x:p.x,y:p.y});
      }
      out.push(pts);
    }
  } finally { host.remove(); }
  return out;
}

function dpSimplify(pts,eps){
  if(pts.length<3) return pts.slice();
  var keep=new Array(pts.length); keep[0]=keep[pts.length-1]=true;
  var stack=[[0,pts.length-1]];
  while(stack.length){
    var seg=stack.pop(), a=seg[0], b=seg[1];
    var ax=pts[a].x, ay=pts[a].y, bx=pts[b].x, by=pts[b].y;
    var dx=bx-ax, dy=by-ay, dd=dx*dx+dy*dy, best=-1, bi=-1;
    for(var i=a+1;i<b;i++){
      var t=dd?((pts[i].x-ax)*dx+(pts[i].y-ay)*dy)/dd:0;
      t=t<0?0:(t>1?1:t);
      var qx=ax+t*dx-pts[i].x, qy=ay+t*dy-pts[i].y, d=qx*qx+qy*qy;
      if(d>best){best=d;bi=i}
    }
    if(best>eps*eps){ keep[bi]=true; stack.push([a,bi],[bi,b]); }
  }
  var o=[]; for(var i=0;i<pts.length;i++) if(keep[i]) o.push(pts[i]);
  return o;
}

function segCount(ps){var n=0;for(var i=0;i<ps.length;i++)n+=ps[i].length-1;return n}

function svgToItems(text,budget){
  var polys=svgPolylines(text);
  if(!polys.length) throw new Error("no drawable geometry in that file");
  // Fit to the tube: bounding box of everything, uniform scale, centred, and
  // Y flipped because SVG counts downward and the device counts up.
  var mnx=1e9,mxx=-1e9,mny=1e9,mxy=-1e9;
  polys.forEach(function(p){p.forEach(function(q){
    if(q.x<mnx)mnx=q.x; if(q.x>mxx)mxx=q.x;
    if(q.y<mny)mny=q.y; if(q.y>mxy)mxy=q.y;})});
  var w=mxx-mnx, h=mxy-mny, span=Math.max(w,h)||1;
  var k=(2*EDGE*0.92)/span, cx=(mnx+mxx)/2, cy=(mny+mxy)/2;
  polys=polys.map(function(p){return p.map(function(q){
    return {x:(q.x-cx)*k, y:-(q.y-cy)*k}})});

  // Binary search the tolerance: the value that fits depends on the drawing,
  // and guessing it either mangles simple art or overflows on complex art.
  var lo=0, hi=2*EDGE, simp=polys;
  if(segCount(polys)>budget){
    for(var it=0; it<24; it++){
      var mid=(lo+hi)/2;
      var t=polys.map(function(p){return dpSimplify(p,mid)});
      if(segCount(t)>budget) lo=mid; else { hi=mid; simp=t; }
    }
  }
  var items=[];
  simp.forEach(function(p){
    for(var i=1;i<p.length;i++)
      items.push({k:"L",x0:Math.round(p[i-1].x),y0:Math.round(p[i-1].y),
                       x1:Math.round(p[i].x),  y1:Math.round(p[i].y)});
  });
  return items;
}

/* ---- scene builder ------------------------------------------------------
   The text is the canonical scene; the canvas is a view over it, so anything
   typed by hand or generated by tools/vec2scene.py still round-trips. Device
   coordinates have +Y up and the SVG has +Y down, so the two differ by a sign
   on y and nothing else — a mirror transform would flip the glyphs too.

   Text footprints are exact, not approximate: ADV is the per-glyph advance
   table lifted straight out of the firmware's font, so the box drawn here is
   the room the stroke font will really take. */
var ADV="626cacc444cc4c2ccccccccccc44cccaccccccccc4ccccccccccccccccc4c4cc4aa8aaaa82882g8aaa88888g888828cc";
function advOf(ch){var c=ch.charCodeAt(0);if(c<32||c>127)c=32;
  var d=ADV.charAt(c-32);
  return d<="9"?+d:(d==="g"?16:10+(d.charCodeAt(0)-97));}
function kernOf(s){return s*(s<40?3:2)}
function inkW(s,t){var w=0;for(var i=0;i<t.length;i++)w+=advOf(t.charAt(i))*s+kernOf(s);
  return t.length?w-kernOf(s):0}
function inkH(s){return s*20}

var items=[], tool="sel", sel=-1, drag=null;
var cv=el("cv");
// Device units, as the firmware uses them. The render multiplies by 3/2 on the
// way to the DAC, so 1200 device units is the rim of the tube (1800 counts) and
// the DAC itself runs out at 1365. Measured on the glass with concentric rings,
// not assumed.
var FIELD=1365, EDGE=1200, CAP=192;

function snapv(v){return el("snap").checked?Math.round(v/25)*25:Math.round(v)}
function pt(ev){var p=cv.createSVGPoint();p.x=ev.clientX;p.y=ev.clientY;
  var m=cv.getScreenCTM().inverse();var q=p.matrixTransform(m);
  return{x:snapv(q.x),y:snapv(-q.y)}}

function ser(){return items.map(function(it){
  if(it.k==="L")return "L "+it.x0+" "+it.y0+" "+it.x1+" "+it.y1;
  if(it.k==="C")return "C "+it.cx+" "+it.cy+" "+it.r;
  if(it.k==="H")return "H "+it.cx+" "+it.cy+" "+it.r0+" "+it.r1+" "+it.src;
  return it.k+" "+it.x+" "+it.y+" "+it.s+" "+it.t;}).join("\n")}

function parse(txt){
  var out=[];
  txt.split("\n").forEach(function(ln){
    ln=ln.trim(); if(!ln)return;
    var k=ln.charAt(0), rest=ln.slice(1).trim();
    var m=rest.match(/^(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*(.*)$/);
    if(k==="L"||k==="H"){
      var f=rest.split(/\s+/).map(Number);
      if(k==="L"&&f.length>=4)out.push({k:"L",x0:f[0],y0:f[1],x1:f[2],y1:f[3]});
      if(k==="H"&&f.length>=4)out.push({k:"H",cx:f[0],cy:f[1],r0:f[2],r1:f[3],src:(f[4]||0)&3});
    }else if(k==="C"){
      var c=rest.split(/\s+/).map(Number);
      if(c.length>=3)out.push({k:"C",cx:c[0],cy:c[1],r:c[2]});
    }else if((k==="T"||k==="D")&&m){
      out.push({k:k,x:+m[1],y:+m[2],s:+m[3],t:m[4]});
    }
  });
  return out;
}

function esc(t){return t.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")}
function outside(it){
  var p=[];
  if(it.k==="L")p=[[it.x0,it.y0],[it.x1,it.y1]];
  else if(it.k==="C")p=[[it.cx-it.r,it.cy],[it.cx+it.r,it.cy],[it.cx,it.cy-it.r],[it.cx,it.cy+it.r]];
  else if(it.k==="H")p=[[it.cx+it.r1,it.cy],[it.cx-it.r1,it.cy],[it.cx,it.cy+it.r1],[it.cx,it.cy-it.r1]];
  else p=[[it.x,it.y],[it.x+inkW(it.s,it.t),it.y+inkH(it.s)]];
  // Against FIELD, not EDGE. EDGE is the dashed advisory ring — the firmware's
  // own banner sits at y=-1190 with ink above it, so ±1200 is guidance for
  // faces rather than a hard limit. FIELD is where the DAC actually runs out,
  // and only that is worth colouring red.
  return p.some(function(q){return Math.abs(q[0])>FIELD||Math.abs(q[1])>FIELD});
}

/* Grab points for the selected item. Returned in device coordinates with the
   field they edit, so one drag handler covers every shape. Sized in device
   units rather than pixels because the SVG scales with the card — 46 units is
   about 7px on a phone and 14px on a desktop, both grabbable. */
var HR=46, HHIT=95;
function handles(it){
  if(!it)return[];
  if(it.k==="L")return[{x:it.x0,y:it.y0,f:"a"},{x:it.x1,y:it.y1,f:"b"}];
  if(it.k==="C")return[{x:it.cx+it.r,y:it.cy,f:"r"}];
  if(it.k==="H")return[{x:it.cx,y:it.cy+it.r1,f:"r1"},{x:it.cx,y:it.cy+it.r0,f:"r0"}];
  return[{x:it.x+inkW(it.s,it.t),y:it.y,f:"s"}];
}

function draw(){
  var g="";
  for(var v=-1000;v<=1000;v+=250)
    g+='<line x1="'+v+'" y1="-1250" x2="'+v+'" y2="1250" class="gr"/>'+
       '<line x1="-1250" y1="'+v+'" x2="1250" y2="'+v+'" class="gr"/>';
  g+='<circle cx="0" cy="0" r="'+FIELD+'" class="fld"/>';
  g+='<circle cx="0" cy="0" r="'+EDGE+'" class="edg"/>';
  items.forEach(function(it,i){
    var cls="it"+(i===sel?" on":"")+(outside(it)?" out":"");
    if(it.k==="L")g+='<line data-i="'+i+'" class="'+cls+'" x1="'+it.x0+'" y1="'+(-it.y0)+'" x2="'+it.x1+'" y2="'+(-it.y1)+'"/>';
    else if(it.k==="C")g+='<circle data-i="'+i+'" class="'+cls+'" cx="'+it.cx+'" cy="'+(-it.cy)+'" r="'+Math.abs(it.r)+'" fill="none"/>';
    else if(it.k==="H"){
      g+='<line data-i="'+i+'" class="'+cls+' hnd" x1="'+it.cx+'" y1="'+(-it.cy)+'" x2="'+it.cx+'" y2="'+(-(it.cy+it.r1))+'"/>';
      g+='<circle class="ghost" cx="'+it.cx+'" cy="'+(-it.cy)+'" r="'+Math.abs(it.r1)+'" fill="none"/>';
    }else{
      var w=inkW(it.s,it.t)||10, h=inkH(it.s);
      g+='<g data-i="'+i+'" class="'+cls+'">'+
         '<rect class="tbox" x="'+it.x+'" y="'+(-(it.y+h))+'" width="'+w+'" height="'+h+'"/>'+
         '<text x="'+it.x+'" y="'+(-it.y)+'" font-size="'+h+'" textLength="'+w+'" '+
         'lengthAdjust="spacingAndGlyphs">'+esc(it.t)+'</text></g>';
    }
  });
  if(tool==="sel"&&sel>=0&&items[sel])
    handles(items[sel]).forEach(function(h,j){
      g+='<circle class="hh" cx="'+h.x+'" cy="'+(-h.y)+'" r="'+HR+'"/>'+
         '<circle class="hhit" data-h="'+j+'" cx="'+h.x+'" cy="'+(-h.y)+'" r="'+HHIT+'"/>';
    });
  if(drag&&drag.prev)g+=drag.prev;
  cv.innerHTML='<style>.gr{stroke:#1b211f;stroke-width:2}.fld{stroke:#2b3331;stroke-width:3;fill:none}'+
    '.edg{stroke:#2b3331;stroke-width:2;fill:none;stroke-dasharray:14 12}'+
    '.it{stroke:#3ddc84;stroke-width:9;fill:none;vector-effect:non-scaling-stroke}'+
    '.it text{fill:#3ddc84;stroke:none;font-family:ui-monospace,monospace}'+
    '.it .tbox{fill:rgba(61,220,132,.09);stroke:none}'+
    '.it.on{stroke:#eaf7f0}.it.on text{fill:#eaf7f0}'+
    '.it.out{stroke:#f0685f}.it.out text{fill:#f0685f}'+
    '.ghost{stroke:#1f3a2c;stroke-width:2;stroke-dasharray:8 10}'+
    '.hnd{stroke-linecap:round}.prev{stroke:#8d9994;stroke-width:6;fill:none;stroke-dasharray:10 8}'+
    '.hh{fill:#eaf7f0;stroke:#0b0d0c;stroke-width:8;pointer-events:none}'+
    '.hhit{fill:transparent;stroke:none;cursor:grab}'+
    '</style>'+g;
  el("cnt").textContent=items.length+" item"+(items.length===1?"":"s")+
    (items.length>CAP?" — over the "+CAP+" the device holds":"");
  el("cnt").className=items.length>CAP?"over":"";
}

/* Live mode: mirror every edit onto the tube.
   
   This is the most link-hostile thing the page can do, so it is throttled by
   completion rather than by a timer. A push is staged in 48-byte chunks with a
   30ms gap between each — a full 192-item scene is 36 chunks, so better than a
   second, and the bridge's web server is blocked for all of it. Firing on an
   interval would pile requests up behind each other and wedge the link, which
   costs minutes to recover.
   
   So: never more than one push in flight, coalesce everything that happened
   while it was out, and settle for 250ms first so a drag or a burst of typing
   becomes one push rather than forty. A failed push turns live mode off instead
   of retrying into a link that is already unhappy. */
var live=false, inflight=false, dirty=false, settleT=null;

function setLive(on){
  live=on;
  el("b-live").className=on?"on":"";
  el("livestat").textContent=on?"live":"";
  if(on)schedule();
}
function schedule(){
  if(!live)return;
  dirty=true;
  if(settleT)return;
  settleT=setTimeout(function(){settleT=null;pump()},250);
}
function pump(){
  if(!live||inflight||!dirty)return;
  dirty=false; inflight=true;
  var t0=Date.now();
  el("livestat").textContent="pushing…";
  fetch("/api/scene",{method:"POST",body:el("scene").value}).then(function(r){
    if(!r.ok)throw 0;
    el("livestat").textContent="live · "+(Date.now()-t0)+"ms";
    // A breath before the next one, so status frames and the ping still fit.
    setTimeout(function(){inflight=false;if(dirty)pump()},150);
  }).catch(function(){
    inflight=false; setLive(false);
    el("livestat").textContent="push failed — live off";
  });
}
el("b-live").onclick=function(){setLive(!live)};
el("b-svg").onclick=function(){el("svgfile").click()};
el("svgfile").onchange=function(){
  var f=this.files&&this.files[0]; if(!f)return;
  var r=new FileReader();
  r.onload=function(){
    try{
      // Leave a little of the cap spare so a clock or a caption can still be
      // added to the drawing afterwards.
      items=svgToItems(r.result, CAP-8);
      sel=-1; sync();
      el("livestat").textContent=items.length+" lines from "+f.name;
    }catch(e){ el("livestat").textContent="SVG: "+e.message; }
  };
  r.readAsText(f);
  this.value="";
};

function sync(push){el("scene").value=ser();draw();if(push!==false)schedule()}
function reparse(){items=parse(el("scene").value);sel=-1;draw()}
el("scene").addEventListener("input",function(){reparse();schedule()});

cv.addEventListener("pointerdown",function(ev){
  var p=pt(ev);
  if(tool==="sel"){
    // A handle sits on top of the shape it belongs to, so test it first.
    var h=ev.target.getAttribute&&ev.target.getAttribute("data-h");
    if(h!==null&&h!==undefined&&sel>=0){
      drag={mode:"handle",h:+h,from:p,orig:JSON.parse(JSON.stringify(items[sel]))};
      cv.setPointerCapture(ev.pointerId);return;
    }
    var t=ev.target.closest?ev.target.closest("[data-i]"):null;
    sel=t?+t.getAttribute("data-i"):-1;
    if(sel>=0)drag={mode:"move",from:p,orig:JSON.parse(JSON.stringify(items[sel]))};
    draw();return;
  }
  if(tool==="T"||tool==="D"){
    var d=tool==="T"?"HELLO":"%H:%M";
    var t=prompt(tool==="T"?"Text":"Clock format (strftime)",d);
    if(t===null||t==="")return;
    items.push({k:tool,x:p.x,y:p.y,s:+el("tscale").value||10,t:t});
    sync();return;
  }
  drag={mode:"new",from:p};
  cv.setPointerCapture(ev.pointerId);
});

cv.addEventListener("pointermove",function(ev){
  var p=pt(ev);
  el("xy").textContent=p.x+", "+p.y;
  if(!drag)return;
  if(drag.mode==="handle"&&sel>=0){
    var it=items[sel], o=drag.orig, f=handles(o)[drag.h].f;
    if(f==="a"){it.x0=p.x;it.y0=p.y}
    else if(f==="b"){it.x1=p.x;it.y1=p.y}
    else if(f==="r"){it.r=Math.max(1,Math.round(Math.hypot(p.x-o.cx,p.y-o.cy)))}
    else if(f==="r1"){it.r1=Math.max(1,Math.round(Math.hypot(p.x-o.cx,p.y-o.cy)))}
    else if(f==="r0"){it.r0=Math.max(0,Math.round(Math.hypot(p.x-o.cx,p.y-o.cy)))}
    else if(f==="s"){
      // Ink width is exactly linear in scale, so the scale that makes the box
      // end under the pointer is one division rather than a search.
      var unit=inkW(1,o.t)||1;
      it.s=Math.max(1,Math.min(60,Math.round((p.x-o.x)/unit)));
    }
    sync(false);return;
  }
  if(drag.mode==="move"&&sel>=0){
    var o=drag.orig, dx=p.x-drag.from.x, dy=p.y-drag.from.y, it=items[sel];
    if(it.k==="L"){it.x0=o.x0+dx;it.y0=o.y0+dy;it.x1=o.x1+dx;it.y1=o.y1+dy}
    else if(it.k==="C"||it.k==="H"){it.cx=o.cx+dx;it.cy=o.cy+dy}
    else{it.x=o.x+dx;it.y=o.y+dy}
    sync(false);return;          // mid-drag: redraw only, push on release
  }
  var a=drag.from, r=Math.round(Math.hypot(p.x-a.x,p.y-a.y));
  if(tool==="L")drag.prev='<line class="prev" x1="'+a.x+'" y1="'+(-a.y)+'" x2="'+p.x+'" y2="'+(-p.y)+'"/>';
  else drag.prev='<circle class="prev" cx="'+a.x+'" cy="'+(-a.y)+'" r="'+r+'"/>';
  draw();
});

function endDrag(ev){
  if(!drag)return;
  if(drag.mode==="new"){
    var a=drag.from,p=pt(ev),r=Math.round(Math.hypot(p.x-a.x,p.y-a.y));
    if(tool==="L"&&(a.x!==p.x||a.y!==p.y))items.push({k:"L",x0:a.x,y0:a.y,x1:p.x,y1:p.y});
    if(tool==="C"&&r>0)items.push({k:"C",cx:a.x,cy:a.y,r:r});
    if(tool==="H"&&r>0)items.push({k:"H",cx:a.x,cy:a.y,r0:0,r1:r,src:+el("hsrc").value});
  }
  drag=null;sync();
}
cv.addEventListener("pointerup",endDrag);
cv.addEventListener("pointercancel",function(){drag=null;draw()});

document.querySelectorAll(".tools .t").forEach(function(b){
  b.onclick=function(){
    document.querySelectorAll(".tools .t").forEach(function(o){o.className="t"});
    b.className="t on"; tool=b.dataset.tool;
    cv.classList.toggle("sel",tool==="sel");
    if(tool!=="sel"){sel=-1;draw()}
  };
});
el("b-del").onclick=function(){if(sel>=0){items.splice(sel,1);sel=-1;sync()}};
el("b-wipe").onclick=function(){items=[];sel=-1;sync()};
document.addEventListener("keydown",function(e){
  if(e.target.tagName==="TEXTAREA"||e.target.tagName==="INPUT")return;
  if((e.key==="Delete"||e.key==="Backspace")&&sel>=0){e.preventDefault();items.splice(sel,1);sel=-1;sync()}
});
reparse();
el("b-relink").onclick=function(){post("/api/relink","1")};
// Symbols for the picker only — the shell data that actually gets drawn lives on
// the Teensy. Chemistry does not drift, so a second copy of 118 symbols is not
// the hazard a second copy of the face list turned out to be.
var SYMS=("H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn "+
"Ga Ge As Se Br Kr Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce Pr Nd "+
"Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn Fr Ra Ac Th "+
"Pa U Np Pu Am Cm Bk Cf Es Fm Md No Lr Rf Db Sg Bh Hs Mt Ds Rg Cn Nh Fl Mc Lv Ts Og").split(" ");
el("elem").innerHTML='<option value="0">cycle</option>'+SYMS.map(function(sy,i){
  return '<option value="'+(i+1)+'">'+(i+1)+' '+sy+'</option>'}).join("");
el("elem").onchange=function(){post("/api/element",this.value)};
// The typeface every face uses unless it asks for a specific one — the digital
// clock keeps its seven-segment numerals whatever is chosen here.
var FONTS=["regular","seven segment","condensed","wide","italic","bold"];
el("font").innerHTML=FONTS.map(function(f,i){
  return '<option value="'+i+'">'+f+'</option>'}).join("");
el("font").onchange=function(){post("/api/font",this.value)};
// Fetched rather than embedded: the names are generated alongside the device's
// star table, so there is no second list here to drift out of order.
fetch("/api/constells").then(function(r){return r.text()}).then(function(t){
  el("con").innerHTML='<option value="0">cycle</option>'+t.split("|").map(
    function(n,i){return '<option value="'+(i+1)+'">'+n+'</option>'}).join("")});
el("con").onchange=function(){post("/api/constell",this.value)};
// Centring. Both axes go in one message so the device never sees a half-applied
// position, and the sliders are the source of truth for what gets sent.
var alX=0, alY=0;
function alShow(){el("alxv").textContent=alX;el("alyv").textContent=alY;
  el("alx").value=alX;el("aly").value=alY;el("alsum").textContent=alX+","+alY}
function alSend(){alShow();post("/api/align",alX+","+alY)}
function alClamp(v){return Math.max(-600,Math.min(600,v))}
el("alx").oninput=function(){alX=+this.value;alShow()};
el("aly").oninput=function(){alY=+this.value;alShow()};
el("alx").onchange=alSend; el("aly").onchange=alSend;
el("alnudgexl").onclick=function(){alX=alClamp(alX-5);alSend()};
el("alnudgexr").onclick=function(){alX=alClamp(alX+5);alSend()};
el("alnudgeyd").onclick=function(){alY=alClamp(alY-5);alSend()};
el("alnudgeyu").onclick=function(){alY=alClamp(alY+5);alSend()};
el("alzero").onclick=function(){alX=0;alY=0;alSend()};
el("altarget").onclick=function(){post("/api/face","align")};
// Minutes since midnight -> HH:MM. -1 means no schedule, and an empty string is
// what <input type=time> wants for "unset".
function hhmm(m){m=+m;if(!isFinite(m)||m<0)return"";
  return ("0"+Math.floor(m/60)).slice(-2)+":"+("0"+(m%60)).slice(-2)}
el("slpsave").onclick=function(){
  var a=el("slpfrom").value,b=el("slpto").value;
  if(a&&b)post("/api/sleepwin",a+"-"+b)};
el("slpoff").onclick=function(){
  el("slpfrom").value="";el("slpto").value="";post("/api/sleepwin","")};
el("fscale").oninput=function(){el("fscaleval").textContent=this.value+"%"};
el("fscale").onchange=function(){post("/api/scale",this.value)};
el("bri").oninput=function(){el("brival").textContent=this.value};
el("bri").onchange=function(){post("/api/brightness",this.value)};

function poll(){
  fetch("/api/state").then(function(r){return r.json()}).then(function(s){
    document.body.classList.remove("offline");
    var mn=s.mode==2?"audio in":(s.mode==1?"pushed scene":"local face");
    var deaf=(s.silent>=0&&s.silent!=65535&&s.silent>20);
    var sick=deaf||!s.rtc||s.sync<0;
    var pc=s.hz?Math.round(s.frame*s.hz/10000):0;
    var bits=[s.hz+"Hz"];
    if(s.frame)bits.push((s.frame/1000).toFixed(1)+"ms "+pc+"%");
    else bits.push("asleep");
    if(s.mqtt)bits.push("MQTT");
    if(s.rssi)bits.push(s.rssi+"dBm");
    setDot(sick?"warn":"ok", bits.join(" \u00b7 "));
    // The clock's own knob and button change the face independently, so the page
    // FOLLOWS the device rather than only its own clicks. First reading also
    // decides which family the drawer opens on.
    if(s.face&&s.face!==curFace){
      curFace=s.face;
      // Follow it into the drawer, every time and not just the first: the knob
      // and the button change the face independently of this page, and the
      // handoff asks for the open family, the selection and the pill to derive
      // from what the device reports. Selecting here pushes immediately, so a
      // page-initiated change has already set these and this is a no-op.
      if(FAMOF[curFace]&&!filt){openFam=FAMOF[curFace];selFace=curFace;drawDrawer()}
      showFace();
    }
    if(document.activeElement!==el("bri")){el("bri").value=s.bri;el("brival").textContent=s.bri}
    // Left alone while it is being dragged, and while the knob is mid-adjust the
    // device is the authority — this just follows it.
    if(s.autonp!==undefined)setTgl("autonp",s.autonp);
    if(s.wobble!==undefined)setTgl("wobble",s.wobble);
    // slpdev is what the DEVICE reports; s.sleep is only what the bridge asked
    // for. Following the device means the checkbox is right when the clock was
    // slept at the knob, and visibly wrong if a SetSleep ever goes missing.
    if(s.slpdev!==undefined)setTgl("sleep",s.slpdev);
    if(s.slpstart!==undefined&&document.activeElement!==el("slpfrom")
       &&document.activeElement!==el("slpto")){
      el("slpfrom").value=hhmm(s.slpstart); el("slpto").value=hhmm(s.slpend);
      el("slpwin").textContent=(s.slpstart<0||s.slpend<0)?"no schedule":
        ("asleep "+hhmm(s.slpstart)+" to "+hhmm(s.slpend));
    }
    if(s.elem!==undefined&&document.activeElement!==el("elem"))el("elem").value=s.elem;
    if(s.font!==undefined&&document.activeElement!==el("font"))el("font").value=s.font;
    if(s.con!==undefined&&document.activeElement!==el("con"))el("con").value=s.con;
    if(s.alx!==undefined&&document.activeElement!==el("alx")
       &&document.activeElement!==el("aly")){alX=s.alx;alY=s.aly;alShow()}
    if(document.activeElement!==el("fscale")&&s.scale!==undefined){
      el("fscale").value=s.scale; el("fscaleval").textContent=s.scale+"%"}
    el("s-mode").textContent=mn;
    var pct=s.hz?Math.round(s.frame*s.hz/10000):0;
    el("s-frame").innerHTML=s.hz+"Hz · "+(s.frame/1000).toFixed(1)+"ms ("+pct+"%)"+
      (pct>100?' <span class="warn">over budget</span>':"");
    el("s-rtc").innerHTML=s.rtc?"ok":'<span class="bad">not responding</span>';
    el("s-sync").innerHTML=(s.sync<0)?'<span class="warn">never</span>':dur(s.sync)+" ago";
    el("s-up").textContent=dur(s.up);
    el("s-mqtt").innerHTML=s.mqtt?"connected":'<span class="warn">offline</span>';
    el("s-wifi").textContent=s.ssid+" · "+s.rssi+"dBm · "+s.ip;
    el("s-silent").innerHTML=(s.silent<0||s.silent==65535)?"--":
      (s.silent+"s ago"+(deaf?' <span class="bad">going deaf</span>':""));
  }).catch(function(){
    document.body.classList.add("offline");
    setDot("bad","bridge unreachable");
  });
}
// ---- link trace ----------------------------------------------------------
// Polled with a cursor rather than re-fetched whole: normally this returns the
// two or three frames since the last poll.
var trSeq=0, trPaused=false, trRows=[];
function trRender(){
  var hideHb=el("tr-hb").checked, hex=el("tr-hex").checked, box=el("trace");
  var atEnd=box.scrollTop+box.clientHeight>=box.scrollHeight-24;
  box.innerHTML=trRows.filter(function(r){return !(hideHb&&r.n=="Status")})
    .slice(-300).map(function(r){
      var arrow=r.d?"&larr;":"&rarr;";
      var t=(r.t/1000).toFixed(2);
      var line='<div><span class="t">'+t+'</span> <span class="'+(r.d?"rx":"tx")+'">'
        +arrow+" "+r.n+"</span> <span class=\"t\">"+r.s+"</span>";
      if(hex&&r.x)line+='<br><span class="b">      '+r.x+"</span>";
      return line+"</div>"}).join("");
  if(atEnd)box.scrollTop=box.scrollHeight;
}
function trPoll(){
  if(trPaused)return;
  fetch("/api/proto?after="+trSeq).then(function(r){return r.json()}).then(function(j){
    if(j.seq<trSeq){trSeq=0;trRows=[]}          // bridge restarted, counter reset
    if(j.f&&j.f.length){
      j.f.forEach(function(r){trRows.push(r);trSeq=r.q});
      if(trRows.length>400)trRows=trRows.slice(-400);
      trRender();
    }
  }).catch(function(){});
}
el("tr-pause").onclick=function(){trPaused=!trPaused;
  this.textContent=trPaused?"resume":"pause"};
el("tr-clear").onclick=function(){trRows=[];trRender()};
el("tr-hb").onchange=trRender; el("tr-hex").onchange=trRender;
trPoll();setInterval(trPoll,1500);

poll();setInterval(poll,2000);
</script></body></html>
)HTMLPAGE";
