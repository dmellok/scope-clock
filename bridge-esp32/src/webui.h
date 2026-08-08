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
  --bg:#0e100f; --card:#161a19; --raised:#1d2321; --line:#272e2c;
  --text:#e8ecea; --muted:#8d9994; --accent:#3ddc84; --warn:#f0b429; --bad:#f0685f;
  --r:10px;
}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;padding:24px 18px 48px;background:var(--bg);color:var(--text);
  font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:960px;margin:0 auto}
code,.mono,td.v,#brival,#scene,.chip{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}

/* header */
header{display:flex;align-items:baseline;justify-content:space-between;
  gap:12px;flex-wrap:wrap;margin-bottom:20px}
h1{font-size:19px;font-weight:650;margin:0;letter-spacing:-.01em}
.pill{display:inline-flex;align-items:center;gap:7px;font-size:13px;color:var(--muted);
  background:var(--card);border:1px solid var(--line);border-radius:999px;padding:5px 12px}
.dot{width:7px;height:7px;border-radius:50%;background:var(--muted);flex:none}
.dot.ok{background:var(--accent);box-shadow:0 0 0 3px rgba(61,220,132,.15)}
.dot.warn{background:var(--warn);box-shadow:0 0 0 3px rgba(240,180,41,.15)}
.dot.bad{background:var(--bad);box-shadow:0 0 0 3px rgba(240,104,95,.15)}

/* cards */
.grid{display:grid;gap:14px;grid-template-columns:1fr}
@media(min-width:800px){.grid{grid-template-columns:1fr 1fr}.span2{grid-column:1/-1}}
section{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:16px 18px}
h2{display:flex;align-items:center;gap:8px;margin:0 0 14px;font-size:12px;font-weight:600;
  text-transform:uppercase;letter-spacing:.07em;color:var(--muted)}
svg.i{width:15px;height:15px;fill:currentColor;flex:none}
.hint{color:var(--muted);font-size:12.5px;line-height:1.55;margin:12px 0 0}
.hint a{color:var(--accent);text-decoration:none}
.hint a:hover{text-decoration:underline}

/* controls */
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
button{font:inherit;font-size:13.5px;color:var(--text);background:var(--raised);
  border:1px solid var(--line);border-radius:7px;padding:8px 14px;cursor:pointer;
  transition:border-color .12s,background .12s,color .12s}
button:hover{border-color:#3a4441;background:#232a28}
button:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
button.primary{border-color:rgba(61,220,132,.45);color:var(--accent)}
button.primary:hover{background:rgba(61,220,132,.10)}

.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{font-size:12px;padding:6px 10px;border-radius:6px;letter-spacing:.01em}
.chip.on{border-color:rgba(61,220,132,.5);color:var(--accent);background:rgba(61,220,132,.12)}

input[type=text],textarea{font:inherit;color:var(--text);background:var(--raised);
  border:1px solid var(--line);border-radius:7px;padding:9px 11px;width:100%}
input[type=text]{flex:1;min-width:11rem}
input:focus,textarea:focus{outline:none;border-color:rgba(61,220,132,.45)}
textarea{font-size:13px;line-height:1.6;height:132px;resize:vertical}
::placeholder{color:#5d6a66}

input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:9rem;
  height:4px;padding:0;background:var(--line);border-radius:2px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:16px;height:16px;
  border-radius:50%;background:var(--accent);border:0;cursor:pointer}
input[type=range]::-moz-range-thumb{width:16px;height:16px;border-radius:50%;
  background:var(--accent);border:0;cursor:pointer}
#brival{min-width:3.1rem;text-align:right;font-size:13px;color:var(--muted)}

/* status */
table{width:100%;border-collapse:collapse;font-size:13.5px}
td{padding:8px 0;border-bottom:1px solid rgba(255,255,255,.045)}
tr:last-child td{border-bottom:0}
td:first-child{color:var(--muted)}
td.v{text-align:right;font-size:13px;overflow-wrap:anywhere}
/* A table will not shrink below its longest unbreakable cell, and the Wi-Fi row
   (SSID, signal and IP, in monospace) is easily wider than a phone. As two
   columns it reads cramped and can push the page sideways; stacked it does not. */
@media(max-width:560px){
  table td{display:block;border-bottom:0;padding:1px 0}
  table td:first-child{font-size:12px}
  table td.v{text-align:left;padding-bottom:9px;
    border-bottom:1px solid rgba(255,255,255,.045)}
  table tr:last-child td.v{border-bottom:0}
}
.warn{color:var(--warn)}
.bad{color:var(--bad)}
body.offline .wrap{opacity:.55;transition:opacity .2s}
footer{margin-top:20px;color:var(--muted);font-size:12.5px}
footer a{color:var(--accent);text-decoration:none}
footer a:hover{text-decoration:underline}
</style></head><body>
<svg style="display:none"><symbol id="i-clock" viewBox="0 0 256 256"><path d="M128,24A104,104,0,1,0,232,128,104.11,104.11,0,0,0,128,24Zm0,192a88,88,0,1,1,88-88A88.1,88.1,0,0,1,128,216Zm64-88a8,8,0,0,1-8,8H128a8,8,0,0,1-8-8V72a8,8,0,0,1,16,0v48h48A8,8,0,0,1,192,128Z"/></symbol><symbol id="i-cube" viewBox="0 0 256 256"><path d="M223.68,66.15,135.68,18h0a15.88,15.88,0,0,0-15.36,0l-88,48.17a16,16,0,0,0-8.32,14v95.64a16,16,0,0,0,8.32,14l88,48.17a15.88,15.88,0,0,0,15.36,0l88-48.17a16,16,0,0,0,8.32-14V80.18A16,16,0,0,0,223.68,66.15ZM128,32h0l80.34,44L128,120,47.66,76ZM40,90l80,43.78v85.79L40,175.82Zm96,129.57V133.82L216,90v85.78Z"/></symbol><symbol id="i-gear" viewBox="0 0 256 256"><path d="M128,80a48,48,0,1,0,48,48A48.05,48.05,0,0,0,128,80Zm0,80a32,32,0,1,1,32-32A32,32,0,0,1,128,160Zm88-29.84q.06-2.16,0-4.32l14.92-18.64a8,8,0,0,0,1.48-7.06,107.21,107.21,0,0,0-10.88-26.25,8,8,0,0,0-6-3.93l-23.72-2.64q-1.48-1.56-3-3L186,40.54a8,8,0,0,0-3.94-6,107.71,107.71,0,0,0-26.25-10.87,8,8,0,0,0-7.06,1.49L130.16,40Q128,40,125.84,40L107.2,25.11a8,8,0,0,0-7.06-1.48A107.6,107.6,0,0,0,73.89,34.51a8,8,0,0,0-3.93,6L67.32,64.27q-1.56,1.49-3,3L40.54,70a8,8,0,0,0-6,3.94,107.71,107.71,0,0,0-10.87,26.25,8,8,0,0,0,1.49,7.06L40,125.84Q40,128,40,130.16L25.11,148.8a8,8,0,0,0-1.48,7.06,107.21,107.21,0,0,0,10.88,26.25,8,8,0,0,0,6,3.93l23.72,2.64q1.49,1.56,3,3L70,215.46a8,8,0,0,0,3.94,6,107.71,107.71,0,0,0,26.25,10.87,8,8,0,0,0,7.06-1.49L125.84,216q2.16.06,4.32,0l18.64,14.92a8,8,0,0,0,7.06,1.48,107.21,107.21,0,0,0,26.25-10.88,8,8,0,0,0,3.93-6l2.64-23.72q1.56-1.48,3-3L215.46,186a8,8,0,0,0,6-3.94,107.71,107.71,0,0,0,10.87-26.25,8,8,0,0,0-1.49-7.06Zm-16.1-6.5a73.93,73.93,0,0,1,0,8.68,8,8,0,0,0,1.74,5.48l14.19,17.73a91.57,91.57,0,0,1-6.23,15L187,173.11a8,8,0,0,0-5.1,2.64,74.11,74.11,0,0,1-6.14,6.14,8,8,0,0,0-2.64,5.1l-2.51,22.58a91.32,91.32,0,0,1-15,6.23l-17.74-14.19a8,8,0,0,0-5-1.75h-.48a73.93,73.93,0,0,1-8.68,0,8,8,0,0,0-5.48,1.74L100.45,215.8a91.57,91.57,0,0,1-15-6.23L82.89,187a8,8,0,0,0-2.64-5.1,74.11,74.11,0,0,1-6.14-6.14,8,8,0,0,0-5.1-2.64L46.43,170.6a91.32,91.32,0,0,1-6.23-15l14.19-17.74a8,8,0,0,0,1.74-5.48,73.93,73.93,0,0,1,0-8.68,8,8,0,0,0-1.74-5.48L40.2,100.45a91.57,91.57,0,0,1,6.23-15L69,82.89a8,8,0,0,0,5.1-2.64,74.11,74.11,0,0,1,6.14-6.14A8,8,0,0,0,82.89,69L85.4,46.43a91.32,91.32,0,0,1,15-6.23l17.74,14.19a8,8,0,0,0,5.48,1.74,73.93,73.93,0,0,1,8.68,0,8,8,0,0,0,5.48-1.74L155.55,40.2a91.57,91.57,0,0,1,15,6.23L173.11,69a8,8,0,0,0,2.64,5.1,74.11,74.11,0,0,1,6.14,6.14,8,8,0,0,0,5.1,2.64l22.58,2.51a91.32,91.32,0,0,1,6.23,15l-14.19,17.74A8,8,0,0,0,199.87,123.66Z"/></symbol><symbol id="i-image" viewBox="0 0 256 256"><path d="M216,40H40A16,16,0,0,0,24,56V200a16,16,0,0,0,16,16H216a16,16,0,0,0,16-16V56A16,16,0,0,0,216,40Zm0,16V158.75l-26.07-26.06a16,16,0,0,0-22.63,0l-20,20-44-44a16,16,0,0,0-22.62,0L40,149.37V56ZM40,172l52-52,80,80H40Zm176,28H194.63l-36-36,20-20L216,181.38V200ZM144,100a12,12,0,1,1,12,12A12,12,0,0,1,144,100Z"/></symbol><symbol id="i-megaphone-simple" viewBox="0 0 256 256"><path d="M228.54,86.66l-176.06-54A16,16,0,0,0,32,48V192a16,16,0,0,0,16,16,16,16,0,0,0,4.52-.65L136,181.73V192a16,16,0,0,0,16,16h32a16,16,0,0,0,16-16v-29.9l28.54-8.75A16.09,16.09,0,0,0,240,138V102A16.09,16.09,0,0,0,228.54,86.66ZM136,165,48,192V48l88,27Zm48,27H152V176.82L184,167Zm40-54-.11,0L152,160.08V79.92l71.89,22,.11,0v36Z"/></symbol><symbol id="i-pulse" viewBox="0 0 256 256"><path d="M240,128a8,8,0,0,1-8,8H204.94l-37.78,75.58A8,8,0,0,1,160,216h-.4a8,8,0,0,1-7.08-5.14L95.35,60.76,63.28,131.31A8,8,0,0,1,56,136H24a8,8,0,0,1,0-16H50.85L88.72,36.69a8,8,0,0,1,14.76.46l57.51,151,31.85-63.71A8,8,0,0,1,200,120h32A8,8,0,0,1,240,128Z"/></symbol><symbol id="i-sun" viewBox="0 0 256 256"><path d="M120,40V16a8,8,0,0,1,16,0V40a8,8,0,0,1-16,0Zm72,88a64,64,0,1,1-64-64A64.07,64.07,0,0,1,192,128Zm-16,0a48,48,0,1,0-48,48A48.05,48.05,0,0,0,176,128ZM58.34,69.66A8,8,0,0,0,69.66,58.34l-16-16A8,8,0,0,0,42.34,53.66Zm0,116.68-16,16a8,8,0,0,0,11.32,11.32l16-16a8,8,0,0,0-11.32-11.32ZM192,72a8,8,0,0,0,5.66-2.34l16-16a8,8,0,0,0-11.32-11.32l-16,16A8,8,0,0,0,192,72Zm5.66,114.34a8,8,0,0,0-11.32,11.32l16,16a8,8,0,0,0,11.32-11.32ZM48,128a8,8,0,0,0-8-8H16a8,8,0,0,0,0,16H40A8,8,0,0,0,48,128Zm80,80a8,8,0,0,0-8,8v24a8,8,0,0,0,16,0V216A8,8,0,0,0,128,208Zm112-88H216a8,8,0,0,0,0,16h24a8,8,0,0,0,0-16Z"/></symbol><symbol id="i-wifi-high" viewBox="0 0 256 256"><path d="M140,204a12,12,0,1,1-12-12A12,12,0,0,1,140,204ZM237.08,87A172,172,0,0,0,18.92,87,8,8,0,0,0,29.08,99.37a156,156,0,0,1,197.84,0A8,8,0,0,0,237.08,87ZM205,122.77a124,124,0,0,0-153.94,0A8,8,0,0,0,61,135.31a108,108,0,0,1,134.06,0,8,8,0,0,0,11.24-1.3A8,8,0,0,0,205,122.77Zm-32.26,35.76a76.05,76.05,0,0,0-89.42,0,8,8,0,0,0,9.42,12.94,60,60,0,0,1,70.58,0,8,8,0,1,0,9.42-12.94Z"/></symbol></svg>

<div class="wrap">
<header>
  <h1>Scope Clock</h1>
  <span class="pill"><span class="dot" id="dot"></span><span id="sub">connecting…</span></span>
</header>

<div class="grid">

<section class="span2">
  <h2><svg class="i"><use href="#i-clock"/></svg>Face</h2>
  <div class="chips" id="faces"></div>
  <p class="hint">The knob on the clock picks the kind of face; the button cycles
    the style within it. Either overrides whatever is pushed.</p>
</section>

<section>
  <h2><svg class="i"><use href="#i-sun"/></svg>Brightness</h2>
  <div class="row">
    <input type="range" id="bri" min="0" max="255" step="5">
    <span id="brival">--</span>
  </div>
  <p class="hint">Beam dwell per dot. The render adapts to fill the refresh, so
    every face ends up equally bright.</p>
</section>

<section>
  <h2><svg class="i"><use href="#i-megaphone-simple"/></svg>Banner</h2>
  <div class="row">
    <input type="text" id="bmsg" placeholder="message" maxlength="60">
    <button class="primary" id="b-send">Send</button>
  </div>
  <p class="hint">Overlaid on whatever is showing, and expires on the device — so
    a bridge that dies cannot strand it on screen.</p>
</section>

<section class="span2">
  <h2><svg class="i"><use href="#i-image"/></svg>Scene</h2>
  <textarea id="scene" spellcheck="false" placeholder="C 0 0 900
L -600 -600 600 600
D -430 -1215 9 %H:%M:%S"></textarea>
  <div class="row" style="margin-top:10px">
    <button class="primary" id="b-push">Push</button>
    <button id="b-clear">Clear</button>
  </div>
  <p class="hint"><code>L</code> line · <code>C</code> circle · <code>T</code> text ·
    <code>D</code> live clock text · <code>H</code> hand (sec/min/hour).
    <code>D</code> and <code>H</code> make it a face template: the device re-renders
    them from its own RTC, so it keeps telling the time with the bridge unplugged.
    Use <code>tools/vec2scene.py</code> to turn artwork into one of these.</p>
</section>

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

</div>

<footer><svg class="i" style="vertical-align:-2px"><use href="#i-gear"/></svg>
  <a href="/config">Network &amp; MQTT settings</a> ·
  icons by <a href="https://phosphoricons.com">Phosphor</a> (MIT)</footer>
</div>

<script>
var FACES=[];
function el(i){return document.getElementById(i)}
function post(p,b){return fetch(p,{method:"POST",body:b}).then(function(){setTimeout(poll,400)})}
function dur(s){s=+s;if(!isFinite(s))return"--";
  if(s<60)return s+"s";if(s<3600)return Math.floor(s/60)+"m "+(s%60)+"s";
  return Math.floor(s/3600)+"h "+Math.floor(s%3600/60)+"m"}
function setDot(cls,txt){el("dot").className="dot "+cls;el("sub").textContent=txt}

// The chips come from the device's own list, so a face added to the firmware
// shows up here without this page being touched. Delegated click, so there is
// no quoting of face names into onclick attributes.
fetch("/api/faces").then(function(r){return r.json()}).then(function(a){
  FACES=a;
  el("faces").innerHTML=a.map(function(f){
    return '<button class="chip" data-f="'+f+'">'+f+'</button>'}).join("");
  poll();
});
el("faces").addEventListener("click",function(e){
  var b=e.target.closest("[data-f]"); if(b) post("/api/face",b.dataset.f)});

el("b-send").onclick=function(){var m=el("bmsg").value.trim();if(m)post("/api/banner",m)};
el("b-push").onclick=function(){post("/api/scene",el("scene").value)};
el("b-clear").onclick=function(){post("/api/scene","")};
el("b-relink").onclick=function(){post("/api/relink","1")};
el("bri").oninput=function(){el("brival").textContent=this.value};
el("bri").onchange=function(){post("/api/brightness",this.value)};

function poll(){
  fetch("/api/state").then(function(r){return r.json()}).then(function(s){
    document.body.classList.remove("offline");
    var mn=s.mode==2?"audio in":(s.mode==1?"pushed scene":"local face");
    var deaf=(s.silent>=0&&s.silent!=65535&&s.silent>20);
    var sick=deaf||!s.rtc||s.sync<0;
    setDot(sick?"warn":"ok", s.face+" · "+mn);
    FACES.forEach(function(f){var b=document.querySelector('[data-f="'+f+'"]');
      if(b)b.className="chip"+(f==s.face?" on":"")});
    if(document.activeElement!==el("bri")){el("bri").value=s.bri;el("brival").textContent=s.bri}
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
poll();setInterval(poll,2000);
</script></body></html>
)HTMLPAGE";
