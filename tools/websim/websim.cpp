// websim.cpp — serves the live beam path to a browser over HTTP.
//
// The rendering itself is core.cpp; this file is only the transport, and the
// WebAssembly build (wasm.cpp) is the other one. A native binary with a small
// server rather than wasm-only because there is nothing to install, it builds
// anywhere there is a compiler including the Pi, and it binds 0.0.0.0 so the
// viewer is any browser on the LAN.
//
// Single-threaded on purpose, with a poll() over a handful of sockets: a
// browser opens two or three connections and holds them idle, and a
// read-per-connection server would block on the idle one while the live one
// waited for its frame.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "core.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace {

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
    const int scale = intParam(query, "s", 70);   // kDefaultScale; core clamps
    const int ms    = intParam(query, "ms", 17);
    if (face < 0 || face >= core::faceCount())
      return response("404 Not Found", "text/plain", "no such face");
    return response("200 OK", "application/octet-stream",
                    core::frame(face, scale, (uint32_t)ms));
  }
  if (path == "/faces") return response("200 OK", "application/json", core::facesJson());
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

  core::init();

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
         core::faceCount(), port);
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
