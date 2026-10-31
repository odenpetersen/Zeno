// JSON parsing, subprocesses, files.
#include "common.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <regex>
#include <thread>
#include <sstream>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

const J& J::operator[](std::string_view k) const {
  static J nil;
  for (auto& [key, v] : o) if (key == k) return v;
  return nil;
}

namespace {
struct Parser {
  std::string_view s; size_t i = 0;
  void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
  static void utf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | cp >> 6); o += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += char(0xE0 | cp >> 12); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
    else { o += char(0xF0 | cp >> 18); o += char(0x80 | ((cp >> 12) & 63)); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
  }
  std::string str() {
    std::string o; ++i;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { o += c; continue; }
      char e = s[i++];
      switch (e) {
        case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
        case 'b': o += '\b'; break; case 'f': o += '\f'; break;
        case 'u': {
          unsigned cp = std::stoul(std::string(s.substr(i, 4)), nullptr, 16); i += 4;
          if (cp >= 0xD800 && cp < 0xDC00 && s.substr(i, 2) == "\\u") { cp = 0x10000 + ((cp - 0xD800) << 10) + (std::stoul(std::string(s.substr(i + 2, 4)), nullptr, 16) - 0xDC00); i += 6; }
          utf8(o, cp); break;
        }
        default: o += e;
      }
    }
    ++i;
    return o;
  }
  J val() {
    ws(); J j;
    if (i >= s.size()) return j;
    char c = s[i];
    if (c == '{') {
      j.t = J::Obj; ++i; ws();
      if (s[i] == '}') { ++i; return j; }
      for (;;) { ws(); std::string k = str(); ws(); ++i; j.o.emplace_back(k, val()); ws(); if (s[i++] == '}') break; }
    } else if (c == '[') {
      j.t = J::Arr; ++i; ws();
      if (s[i] == ']') { ++i; return j; }
      for (;;) { j.a.push_back(val()); ws(); if (s[i++] == ']') break; }
    } else if (c == '"') { j.t = J::Str; j.s = str(); }
    else if (s.substr(i, 4) == "true") { j.t = J::Bool; j.b = true; i += 4; }
    else if (s.substr(i, 5) == "false") { j.t = J::Bool; i += 5; }
    else if (s.substr(i, 4) == "null") { i += 4; }
    else { j.t = J::Num; char* e; j.n = std::strtod(s.data() + i, &e); i = e - s.data(); }
    return j;
  }
};
}  // namespace

J J::parse(std::string_view text) { Parser p{text}; return p.val(); }

std::string J::dump() const {
  switch (t) {
    case Null: return "null";
    case Bool: return b ? "true" : "false";
    case Num: { char buf[32]; std::snprintf(buf, 32, "%.17g", n); return buf; }
    case Str: return jq(s);
    case Arr: { std::string o = "["; for (auto& v : a) o += (o.size() > 1 ? "," : "") + v.dump(); return o + "]"; }
    case Obj: { std::string o = "{"; for (auto& [k, v] : this->o) o += (o.size() > 1 ? "," : "") + jq(k) + ":" + v.dump(); return o + "}"; }
  }
  return "null";
}

std::string jq(std::string_view v) {
  std::string o = "\"";
  for (unsigned char c : v) {
    if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
    else if (c == '\n') o += "\\n";
    else if (c == '\t') o += "\\t";
    else if (c < 0x20 || c == 0x7f) { char b[8]; std::snprintf(b, 8, "\\u%04x", c); o += b; }
    else o += char(c);
  }
  return o + "\"";
}

Proc run_cmd(const std::vector<std::string>& argv, const std::string& input) {
  Proc r;
  auto t0 = std::chrono::steady_clock::now();
  int out[2], in[2];
  if (pipe2(out, O_CLOEXEC) || pipe2(in, O_CLOEXEC)) return r;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in[0], 0);
  posix_spawn_file_actions_adddup2(&fa, out[1], 1);
  posix_spawn_file_actions_adddup2(&fa, out[1], 2);
  std::vector<char*> av;
  for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
  av.push_back(nullptr);
  pid_t pid;
  int rc = posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  close(out[1]); close(in[0]);
  if (rc != 0) { close(out[0]); close(in[1]); r.out = "cannot run " + argv[0] + ": " + strerror(rc); return r; }
  size_t w = 0;
  fcntl(in[1], F_SETFL, O_NONBLOCK);
  if (input.empty()) { close(in[1]); in[1] = -1; }
  char buf[65536];
  for (;;) {
    pollfd p[2] = {{out[0], POLLIN, 0}, {in[1], POLLOUT, 0}};
    poll(p, in[1] >= 0 ? 2 : 1, -1);
    if (in[1] >= 0 && (p[1].revents & (POLLOUT | POLLERR))) {
      ssize_t k = write(in[1], input.data() + w, input.size() - w);
      if (k > 0) w += k;
      if (k < 0 || w == input.size()) { close(in[1]); in[1] = -1; }
    }
    if (p[0].revents) { ssize_t k = read(out[0], buf, sizeof buf); if (k <= 0) break; r.out.append(buf, k); }
  }
  if (in[1] >= 0) close(in[1]);
  close(out[0]);
  waitpid(pid, &r.status, 0);
  r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return r;
}

int spawn_stream(const std::vector<std::string>& argv, const std::string& input, const std::function<void(const std::string&)>& on) {
  int out[2], in[2];
  if (pipe2(out, O_CLOEXEC) || pipe2(in, O_CLOEXEC)) return -1;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in[0], 0);
  posix_spawn_file_actions_adddup2(&fa, out[1], 1);
  std::vector<char*> av;
  for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
  av.push_back(nullptr);
  pid_t pid;
  int rc = posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  close(out[1]); close(in[0]);
  if (rc) { close(out[0]); close(in[1]); return -1; }
  std::thread w([&] { for (size_t d = 0; d < input.size();) { ssize_t k = write(in[1], input.data() + d, input.size() - d); if (k <= 0) break; d += k; } close(in[1]); });
  char buf[16384];
  for (ssize_t k; (k = read(out[0], buf, sizeof buf)) > 0;) on(std::string(buf, k));
  w.join(); close(out[0]);
  int st = 0; waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

std::string read_file(const std::string& path) { std::ifstream f(path, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
bool write_file(const std::string& path, std::string_view data) { std::ofstream f(path, std::ios::binary); f << data; return bool(f); }
std::string self_dir() {
  char b[4096]; ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
  std::string p(b, n > 0 ? n : 0);
  return p.substr(0, p.rfind('/'));
}
std::string strip_cell_ns(std::string s) {
  static const std::regex re("(__c[0-9_]+::)+");
  return std::regex_replace(s, re, "");
}
