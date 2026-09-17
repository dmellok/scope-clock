// websim.cpp — the clock's real render path, drawn live in a browser.
//
// Same seam as tools/hostsim: the only firmware code that touches hardware is
// hal::dac::write/blank, so vector.cpp, text.cpp and every face compile for the
// host against a fake DAC that records the beam path instead of driving one.
// hostsim MEASURES that path and prints numbers; this serves it, frame by
// frame, to a canvas. Nothing about the geometry is reimplemented — what the
// browser draws is what the tube would draw, dot for dot.
//
// Why a native binary serving HTTP rather than WebAssembly: nothing to install
// (no emsdk, no node), it builds anywhere there is a compiler including the Pi,
// and the viewer is any browser on the LAN. The wasm route stays open because
// renderFrameBlob() below is the only thing the server calls — swapping the
// transport does not touch the rendering.
//
// Single-threaded on purpose, with a poll() over a handful of sockets: a
// browser opens two or three connections and holds them idle, and a
// read-per-connection server would block on the idle one while the live one
// waited for its frame.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "face.h"
#include "state.h"
#include "drawlist.h"
#include "vector.h"
#include "text.h"
#include "sim.h"
#include "seed.h"
#include "names.h"
#include <Arduino.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace {

// ---- the clock the faces read ----------------------------------------------
// Real wall time, refreshed every frame, so the clock faces show the actual
// time rather than a frozen fixture. It is the one thing a static thumbnail
// cannot show and a live viewer should.
ClockState g_clk;

void refreshClock() {
  const time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  g_clk.year   = (int16_t)(lt.tm_year - 100);
  g_clk.month  = (int16_t)(lt.tm_mon + 1);
  g_clk.day    = (int16_t)lt.tm_mday;
  g_clk.wday   = (int16_t)lt.tm_wday;
  g_clk.hour   = (int16_t)lt.tm_hour;
  g_clk.minute = (int16_t)lt.tm_min;
  g_clk.second = (int16_t)lt.tm_sec;
  g_clk.rtcPresent = true;
  g_clk.everSet    = true;
}

// ---- frame production -------------------------------------------------------
int      g_lastFace = -1;
int      g_audioStep = 0;
DeviceState g_dev;

// Some faces ACCUMULATE: the Lorenz attractor has no trail until it has been
// integrated for a while and the digital rain starts with empty columns. Cold
// frames of those are blank, which reads as a broken viewer rather than a face
// that needs a moment, so a face change is warmed before it is shown.
constexpr int kWarmFrames = 40;

void renderOnce(int faceId, int scalePct, uint32_t stepMs) {
  refreshClock();
  feedAudio(g_audioStep++);
  g_dev.faceId = (uint8_t)faceId;
  DrawList d;
  faces::current(g_dev)(g_clk, d);
  // The device's own order, from main.cpp: resolve text positions FIRST, then
  // apply the per-face size. Skipping centerLines is not a no-op for any face
  // that stacks rows at (0,0) and lets the layout place them — the digital
  // face's four items land on top of each other and right of centre. The
  // thumbnail generator had exactly that bug, which is how this was found.
  txt::centerLines(d);
  if (!faces::rawScale((uint8_t)faceId)) scaleList(d, scalePct);
  vec::renderFrame(d);
  simStepMillis(stepMs);
}

void put16(std::string& b, int v) {
  b.push_back((char)(v & 0xFF));
  b.push_back((char)((v >> 8) & 0xFF));
}
void put32(std::string& b, int32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back((char)((v >> (i * 8)) & 0xFF));
}

// One frame, as the beam drew it:
//   u16 strokes, u32 dots, u32 moves, i32 maxR, u16 face, u16 pad
//   per stroke: u16 points, then points x (i16 x, i16 y), DAC counts about 0
//
// Sent at full resolution, every dot. The thumbnail generator simplifies its
// polylines to fit an ESP32's flash; here the dot spacing IS the interesting
// part, because it is what brightness is made of. A dense face is about 170KB
// a frame, which localhost and any wired LAN swallow without noticing.
std::string renderFrameBlob(int faceId, int scalePct, uint32_t stepMs) {
  if (faceId != g_lastFace) {
    for (int i = 0; i < kWarmFrames; ++i) renderOnce(faceId, scalePct, stepMs);
    g_lastFace = faceId;
  }

  sim::reset();
  sim::captureBegin();
  renderOnce(faceId, scalePct, stepMs);
  sim::captureEnd();
  const sim::Stats st = sim::get();

  std::string b;
  const int n = sim::strokeCount();
  put16(b, n);
  put32(b, (int32_t)st.dots);
  put32(b, (int32_t)st.moves);
  put32(b, st.maxR);
  put16(b, faceId);
  put16(b, 0);
  for (int i = 0; i < n; ++i) {
    const int pts = sim::strokePoints(i);
    put16(b, pts);
    for (int j = 0; j < pts; ++j) {
      const sim::Pt p = sim::strokePoint(i, j);
      put16(b, p.x); put16(b, p.y);
    }
  }
  return b;
}

// ---- a very small HTTP server ----------------------------------------------

std::string readFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return {};
  std::string s;
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

int intParam(const std::string& q, const char* key, int dflt) {
  const std::string k = std::string(key) + "=";
  size_t at = q.find(k);
  if (at == std::string::npos) return dflt;
  return atoi(q.c_str() + at + k.size());
}

std::string response(const char* status, const char* type, const std::string& body) {
  char head[256];
  snprintf(head, sizeof head,
           "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
           "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n",
           status, type, body.size());
  return std::string(head) + body;
}

std::string facesJson() {
  std::string j = "[";
  for (int i = 0; i < kNameCount; ++i) {
    if (i) j += ",";
    j += "{\"n\":\""; j += kNames[i][0];
    j += "\",\"f\":\""; j += kNames[i][1]; j += "\"}";
  }
  return j + "]";
}

std::string handle(const std::string& req, const char* dir) {
  const size_t sp = req.find(' ');
  if (sp == std::string::npos) return response("400 Bad Request", "text/plain", "");
  const size_t sp2 = req.find(' ', sp + 1);
  std::string path = req.substr(sp + 1, sp2 - sp - 1);
  std::string query;
  const size_t q = path.find('?');
  if (q != std::string::npos) { query = path.substr(q + 1); path = path.substr(0, q); }

  if (path == "/frame") {
    const int face  = intParam(query, "f", 0);
    const int scale = intParam(query, "s", DeviceState::kDefaultScale);
    const int ms    = intParam(query, "ms", 17);
    if (face < 0 || face >= faces::count())
      return response("404 Not Found", "text/plain", "no such face");
    return response("200 OK", "application/octet-stream",
                    renderFrameBlob(face, scale, (uint32_t)ms));
  }
  if (path == "/faces") return response("200 OK", "application/json", facesJson());
  if (path == "/" || path == "/index.html") {
    const std::string html = readFile((std::string(dir) + "/index.html").c_str());
    if (html.empty()) return response("500 Internal Server Error", "text/plain",
                                      "index.html missing next to the binary");
    return response("200 OK", "text/html; charset=utf-8", html);
  }
  return response("404 Not Found", "text/plain", "");
}

}  // namespace

int main(int argc, char** argv) {
  const int port = argc > 1 ? atoi(argv[1]) : 8080;
  const char* dir = WEBSIM_DIR;

  vec::init();
  hal::midi::seed();
  seedHostFaces();

  signal(SIGPIPE, SIG_IGN);   // a browser closing a tab mid-frame is normal

  const int srv = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);   // the LAN, not just localhost
  a.sin_port = htons((uint16_t)port);
  if (bind(srv, (sockaddr*)&a, sizeof a) < 0) { perror("bind"); return 1; }
  listen(srv, 8);

  printf("websim: %d faces on http://localhost:%d/ (and this machine's LAN address)\n",
         faces::count(), port);
  fflush(stdout);

  std::vector<pollfd> fds{{srv, POLLIN, 0}};
  std::vector<std::string> in(1);

  for (;;) {
    if (poll(fds.data(), fds.size(), -1) < 0) break;

    if (fds[0].revents & POLLIN) {
      const int c = accept(srv, nullptr, nullptr);
      if (c >= 0) {
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        if (fds.size() < 16) { fds.push_back({c, POLLIN, 0}); in.push_back({}); }
        else close(c);
      }
    }

    for (size_t i = 1; i < fds.size();) {
      bool drop = false;
      if (fds[i].revents & (POLLIN | POLLHUP)) {
        char buf[4096];
        const ssize_t n = recv(fds[i].fd, buf, sizeof buf, 0);
        if (n <= 0) drop = true;
        else {
          in[i].append(buf, (size_t)n);
          // One request per pass is enough: the page never pipelines, and
          // holding the loop here would starve the accept above.
          const size_t end = in[i].find("\r\n\r\n");
          if (end != std::string::npos) {
            const std::string out = handle(in[i], dir);
            in[i].erase(0, end + 4);
            size_t sent = 0;
            while (sent < out.size()) {
              const ssize_t w = send(fds[i].fd, out.data() + sent, out.size() - sent, 0);
              if (w <= 0) { drop = true; break; }
              sent += (size_t)w;
            }
          }
        }
      }
      if (drop) {
        close(fds[i].fd);
        fds.erase(fds.begin() + (long)i);
        in.erase(in.begin() + (long)i);
      } else ++i;
    }
  }
  return 0;
}
