// webui.h — the control panel the bridge serves, held in flash.
//
// Everything is inline: no CDN, no external font, no second request. The clock
// may well live on a network with no route to the internet, and a control page
// that only works when GitHub is reachable is not a control page.
//
// Icons are Phosphor (https://phosphoricons.com), MIT licensed, embedded as an
// SVG sprite. Which is a better joke than it looks: phosphor is also what the
// tube is coated in.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <pgmspace.h>

static const char WEB_UI[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Scope Clock</title>
<style>
:root{--p:#39ff88;--dim:#1c7a45;--bg:#050805}
*{box-sizing:border-box}
body{margin:0;padding:1.2rem;background:var(--bg);color:var(--p);
  font:14px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  text-shadow:0 0 6px rgba(57,255,136,.55);min-height:100vh}
/* scanlines + a slow flicker, the two things that read as "CRT" */
body::before{content:"";position:fixed;inset:0;pointer-events:none;z-index:9;
  background:repeating-linear-gradient(180deg,rgba(0,0,0,0) 0 2px,rgba(0,0,0,.28) 2px 4px)}
body::after{content:"";position:fixed;inset:0;pointer-events:none;z-index:8;
  background:radial-gradient(ellipse at center,rgba(0,0,0,0) 55%,rgba(0,0,0,.55) 100%);
  animation:fl 5s infinite}
@keyframes fl{0%,97%,100%{opacity:1}98%{opacity:.9}}
h1{font-size:1.1rem;letter-spacing:.22em;margin:0 0 .2rem;text-transform:uppercase}
.sub{color:var(--dim);margin:0 0 1.4rem;font-size:.8rem;letter-spacing:.1em}
fieldset{border:1px solid var(--dim);margin:0 0 1rem;padding:.2rem 1rem 1rem;
  background:rgba(57,255,136,.03)}
legend{padding:0 .5rem;display:flex;align-items:center;gap:.45rem;
  text-transform:uppercase;letter-spacing:.16em;font-size:.78rem}
svg.i{width:1.05em;height:1.05em;fill:currentColor;vertical-align:-.15em;
  filter:drop-shadow(0 0 4px rgba(57,255,136,.6))}
.row{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center}
button,select,input,textarea{font:inherit;color:var(--p);background:#0a120c;
  border:1px solid var(--dim);padding:.5rem .7rem;text-shadow:inherit}
button{cursor:pointer;letter-spacing:.08em;text-transform:uppercase}
button:hover,button:focus{background:var(--p);color:#04160a;text-shadow:none;outline:0}
button.on{background:var(--p);color:#04160a;text-shadow:none}
input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:9rem;
  height:.4rem;padding:0;background:var(--dim)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:1rem;height:1.5rem;
  background:var(--p);border:0;box-shadow:0 0 8px var(--p);cursor:pointer}
input[type=text],textarea{flex:1;min-width:12rem;width:100%}
textarea{height:8.5rem;resize:vertical;line-height:1.45}
table{width:100%;border-collapse:collapse;font-size:.86rem}
td{padding:.28rem 0;border-bottom:1px dotted rgba(57,255,136,.16)}
td:last-child{text-align:right;color:#bfffd8}
.warn{color:#ffb300;text-shadow:0 0 6px rgba(255,179,0,.6)}
.hint{color:var(--dim);font-size:.76rem;margin:.5rem 0 0;line-height:1.5}
a{color:var(--dim)}
</style></head><body>
<svg style="display:none"><symbol id="i-clock" viewBox="0 0 256 256"><path d="M128,24A104,104,0,1,0,232,128,104.11,104.11,0,0,0,128,24Zm0,192a88,88,0,1,1,88-88A88.1,88.1,0,0,1,128,216Zm64-88a8,8,0,0,1-8,8H128a8,8,0,0,1-8-8V72a8,8,0,0,1,16,0v48h48A8,8,0,0,1,192,128Z"/></symbol><symbol id="i-cube" viewBox="0 0 256 256"><path d="M223.68,66.15,135.68,18h0a15.88,15.88,0,0,0-15.36,0l-88,48.17a16,16,0,0,0-8.32,14v95.64a16,16,0,0,0,8.32,14l88,48.17a15.88,15.88,0,0,0,15.36,0l88-48.17a16,16,0,0,0,8.32-14V80.18A16,16,0,0,0,223.68,66.15ZM128,32h0l80.34,44L128,120,47.66,76ZM40,90l80,43.78v85.79L40,175.82Zm96,129.57V133.82L216,90v85.78Z"/></symbol><symbol id="i-gear" viewBox="0 0 256 256"><path d="M128,80a48,48,0,1,0,48,48A48.05,48.05,0,0,0,128,80Zm0,80a32,32,0,1,1,32-32A32,32,0,0,1,128,160Zm88-29.84q.06-2.16,0-4.32l14.92-18.64a8,8,0,0,0,1.48-7.06,107.21,107.21,0,0,0-10.88-26.25,8,8,0,0,0-6-3.93l-23.72-2.64q-1.48-1.56-3-3L186,40.54a8,8,0,0,0-3.94-6,107.71,107.71,0,0,0-26.25-10.87,8,8,0,0,0-7.06,1.49L130.16,40Q128,40,125.84,40L107.2,25.11a8,8,0,0,0-7.06-1.48A107.6,107.6,0,0,0,73.89,34.51a8,8,0,0,0-3.93,6L67.32,64.27q-1.56,1.49-3,3L40.54,70a8,8,0,0,0-6,3.94,107.71,107.71,0,0,0-10.87,26.25,8,8,0,0,0,1.49,7.06L40,125.84Q40,128,40,130.16L25.11,148.8a8,8,0,0,0-1.48,7.06,107.21,107.21,0,0,0,10.88,26.25,8,8,0,0,0,6,3.93l23.72,2.64q1.49,1.56,3,3L70,215.46a8,8,0,0,0,3.94,6,107.71,107.71,0,0,0,26.25,10.87,8,8,0,0,0,7.06-1.49L125.84,216q2.16.06,4.32,0l18.64,14.92a8,8,0,0,0,7.06,1.48,107.21,107.21,0,0,0,26.25-10.88,8,8,0,0,0,3.93-6l2.64-23.72q1.56-1.48,3-3L215.46,186a8,8,0,0,0,6-3.94,107.71,107.71,0,0,0,10.87-26.25,8,8,0,0,0-1.49-7.06Zm-16.1-6.5a73.93,73.93,0,0,1,0,8.68,8,8,0,0,0,1.74,5.48l14.19,17.73a91.57,91.57,0,0,1-6.23,15L187,173.11a8,8,0,0,0-5.1,2.64,74.11,74.11,0,0,1-6.14,6.14,8,8,0,0,0-2.64,5.1l-2.51,22.58a91.32,91.32,0,0,1-15,6.23l-17.74-14.19a8,8,0,0,0-5-1.75h-.48a73.93,73.93,0,0,1-8.68,0,8,8,0,0,0-5.48,1.74L100.45,215.8a91.57,91.57,0,0,1-15-6.23L82.89,187a8,8,0,0,0-2.64-5.1,74.11,74.11,0,0,1-6.14-6.14,8,8,0,0,0-5.1-2.64L46.43,170.6a91.32,91.32,0,0,1-6.23-15l14.19-17.74a8,8,0,0,0,1.74-5.48,73.93,73.93,0,0,1,0-8.68,8,8,0,0,0-1.74-5.48L40.2,100.45a91.57,91.57,0,0,1,6.23-15L69,82.89a8,8,0,0,0,5.1-2.64,74.11,74.11,0,0,1,6.14-6.14A8,8,0,0,0,82.89,69L85.4,46.43a91.32,91.32,0,0,1,15-6.23l17.74,14.19a8,8,0,0,0,5.48,1.74,73.93,73.93,0,0,1,8.68,0,8,8,0,0,0,5.48-1.74L155.55,40.2a91.57,91.57,0,0,1,15,6.23L173.11,69a8,8,0,0,0,2.64,5.1,74.11,74.11,0,0,1,6.14,6.14,8,8,0,0,0,5.1,2.64l22.58,2.51a91.32,91.32,0,0,1,6.23,15l-14.19,17.74A8,8,0,0,0,199.87,123.66Z"/></symbol><symbol id="i-image" viewBox="0 0 256 256"><path d="M216,40H40A16,16,0,0,0,24,56V200a16,16,0,0,0,16,16H216a16,16,0,0,0,16-16V56A16,16,0,0,0,216,40Zm0,16V158.75l-26.07-26.06a16,16,0,0,0-22.63,0l-20,20-44-44a16,16,0,0,0-22.62,0L40,149.37V56ZM40,172l52-52,80,80H40Zm176,28H194.63l-36-36,20-20L216,181.38V200ZM144,100a12,12,0,1,1,12,12A12,12,0,0,1,144,100Z"/></symbol><symbol id="i-megaphone-simple" viewBox="0 0 256 256"><path d="M228.54,86.66l-176.06-54A16,16,0,0,0,32,48V192a16,16,0,0,0,16,16,16,16,0,0,0,4.52-.65L136,181.73V192a16,16,0,0,0,16,16h32a16,16,0,0,0,16-16v-29.9l28.54-8.75A16.09,16.09,0,0,0,240,138V102A16.09,16.09,0,0,0,228.54,86.66ZM136,165,48,192V48l88,27Zm48,27H152V176.82L184,167Zm40-54-.11,0L152,160.08V79.92l71.89,22,.11,0v36Z"/></symbol><symbol id="i-pulse" viewBox="0 0 256 256"><path d="M240,128a8,8,0,0,1-8,8H204.94l-37.78,75.58A8,8,0,0,1,160,216h-.4a8,8,0,0,1-7.08-5.14L95.35,60.76,63.28,131.31A8,8,0,0,1,56,136H24a8,8,0,0,1,0-16H50.85L88.72,36.69a8,8,0,0,1,14.76.46l57.51,151,31.85-63.71A8,8,0,0,1,200,120h32A8,8,0,0,1,240,128Z"/></symbol><symbol id="i-sun" viewBox="0 0 256 256"><path d="M120,40V16a8,8,0,0,1,16,0V40a8,8,0,0,1-16,0Zm72,88a64,64,0,1,1-64-64A64.07,64.07,0,0,1,192,128Zm-16,0a48,48,0,1,0-48,48A48.05,48.05,0,0,0,176,128ZM58.34,69.66A8,8,0,0,0,69.66,58.34l-16-16A8,8,0,0,0,42.34,53.66Zm0,116.68-16,16a8,8,0,0,0,11.32,11.32l16-16a8,8,0,0,0-11.32-11.32ZM192,72a8,8,0,0,0,5.66-2.34l16-16a8,8,0,0,0-11.32-11.32l-16,16A8,8,0,0,0,192,72Zm5.66,114.34a8,8,0,0,0-11.32,11.32l16,16a8,8,0,0,0,11.32-11.32ZM48,128a8,8,0,0,0-8-8H16a8,8,0,0,0,0,16H40A8,8,0,0,0,48,128Zm80,80a8,8,0,0,0-8,8v24a8,8,0,0,0,16,0V216A8,8,0,0,0,128,208Zm112-88H216a8,8,0,0,0,0,16h24a8,8,0,0,0,0-16Z"/></symbol><symbol id="i-wifi-high" viewBox="0 0 256 256"><path d="M140,204a12,12,0,1,1-12-12A12,12,0,0,1,140,204ZM237.08,87A172,172,0,0,0,18.92,87,8,8,0,0,0,29.08,99.37a156,156,0,0,1,197.84,0A8,8,0,0,0,237.08,87ZM205,122.77a124,124,0,0,0-153.94,0A8,8,0,0,0,61,135.31a108,108,0,0,1,134.06,0,8,8,0,0,0,11.24-1.3A8,8,0,0,0,205,122.77Zm-32.26,35.76a76.05,76.05,0,0,0-89.42,0,8,8,0,0,0,9.42,12.94,60,60,0,0,1,70.58,0,8,8,0,1,0,9.42-12.94Z"/></symbol></svg>

<h1>Scope&nbsp;Clock</h1>
<p class="sub" id="sub">connecting&hellip;</p>

<fieldset><legend><svg class="i"><use href="#i-clock"/></svg>Face</legend>
  <div class="row" id="faces"></div>
  <p class="hint">The knob on the clock picks the kind of face; the button
    cycles the style within it. Either overrides whatever is pushed.</p>
</fieldset>

<fieldset><legend><svg class="i"><use href="#i-sun"/></svg>Brightness</legend>
  <div class="row">
    <input type="range" id="bri" min="0" max="255" step="5">
    <span id="brival" style="min-width:2.6rem;text-align:right">--</span>
  </div>
  <p class="hint">Beam dwell per dot. The render adapts to fill the refresh, so
    every face ends up equally bright.</p>
</fieldset>

<fieldset><legend><svg class="i"><use href="#i-megaphone-simple"/></svg>Banner</legend>
  <div class="row">
    <input type="text" id="bmsg" placeholder="message" maxlength="60">
    <button onclick="banner()">Send</button>
  </div>
  <p class="hint">Overlaid on whatever is showing and expires on the device, so
    a bridge that dies cannot strand it on screen.</p>
</fieldset>

<fieldset><legend><svg class="i"><use href="#i-image"/></svg>Scene</legend>
  <textarea id="scene" spellcheck="false" placeholder="C 0 0 900
L -600 -600 600 600
D -430 -1215 9 %H:%M:%S"></textarea>
  <div class="row" style="margin-top:.5rem">
    <button onclick="scene()">Push</button>
    <button onclick="clearScene()">Clear</button>
  </div>
  <p class="hint">L line &middot; C circle &middot; T text &middot;
    D live clock text &middot; H hand (sec/min/hour).
    D and H make it a face template: the device re-renders them from its own
    RTC, so it keeps telling the time with the bridge unplugged.
    Use tools/vec2scene.py to turn artwork into one of these.</p>
</fieldset>

<fieldset><legend><svg class="i"><use href="#i-pulse"/></svg>Status</legend>
  <table>
    <tr><td>Mode</td><td id="s-mode">--</td></tr>
    <tr><td>Refresh</td><td id="s-frame">--</td></tr>
    <tr><td>RTC</td><td id="s-rtc">--</td></tr>
    <tr><td>Last time sync</td><td id="s-sync">--</td></tr>
    <tr><td>Device uptime</td><td id="s-up">--</td></tr>
    <tr><td>MQTT</td><td id="s-mqtt">--</td></tr>
    <tr><td>Wi-Fi</td><td id="s-wifi">--</td></tr>
  </table>
</fieldset>

<p class="hint"><svg class="i"><use href="#i-gear"/></svg>
  <a href="/config">Network &amp; MQTT settings</a> &middot;
  icons by <a href="https://phosphoricons.com">Phosphor</a> (MIT)</p>

<script>
var FACES=["hands","numbers","digital","datetime","cube","lissajous","starfield"];
function el(i){return document.getElementById(i)}
function post(p,b){return fetch(p,{method:"POST",body:b}).then(function(){setTimeout(poll,400)})}
function dur(s){s=+s;if(!isFinite(s))return"--";
  if(s<60)return s+"s";if(s<3600)return Math.floor(s/60)+"m "+(s%60)+"s";
  return Math.floor(s/3600)+"h "+Math.floor(s%3600/60)+"m"}

el("faces").innerHTML=FACES.map(function(f){
  return '<button id="f-'+f+'" onclick="face(\''+f+'\')">'+f+'</button>'}).join("");
function face(f){post("/api/face",f)}
function banner(){var m=el("bmsg").value.trim();if(m)post("/api/banner",m)}
function scene(){post("/api/scene",el("scene").value)}
function clearScene(){post("/api/scene","")}
el("bri").oninput=function(){el("brival").textContent=this.value};
el("bri").onchange=function(){post("/api/brightness",this.value)};

function poll(){
  fetch("/api/state").then(function(r){return r.json()}).then(function(s){
    el("sub").textContent=s.face+" · "+(s.mode?"pushed scene":"local face");
    FACES.forEach(function(f){var b=el("f-"+f);if(b)b.className=(f==s.face)?"on":""});
    if(document.activeElement!==el("bri")){el("bri").value=s.bri;el("brival").textContent=s.bri}
    el("s-mode").textContent=s.mode?"pushed":"face";
    var pct=s.hz?Math.round(s.frame*s.hz/10000):0;
    el("s-frame").innerHTML=s.hz+"Hz · "+(s.frame/1000).toFixed(1)+"ms ("+pct+"%)"+
      (pct>100?' <span class="warn">over budget</span>':"");
    el("s-rtc").innerHTML=s.rtc?"ok":'<span class="warn">not responding</span>';
    el("s-sync").innerHTML=(s.sync<0)?'<span class="warn">never</span>':dur(s.sync)+" ago";
    el("s-up").textContent=dur(s.up);
    el("s-mqtt").innerHTML=s.mqtt?"connected":'<span class="warn">offline</span>';
    el("s-wifi").textContent=s.ssid+" · "+s.rssi+"dBm · "+s.ip;
  }).catch(function(){el("sub").textContent="bridge unreachable"});
}
poll();setInterval(poll,2000);
</script></body></html>
)HTMLPAGE";
