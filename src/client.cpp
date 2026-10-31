// Frontend side of the kernel protocol.
#include "common.hpp"
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

void Kernel::start() {
  int rq[2], o[2], e[2];
  if (pipe2(rq, O_CLOEXEC) || pipe2(o, O_CLOEXEC) || pipe2(e, O_CLOEXEC)) { perror("pipe"); exit(1); }
  pid = fork();
  if (pid == 0) {
    int null = open("/dev/null", O_RDONLY);
    dup2(null, 0); dup2(o[1], 1); dup2(e[1], 2);
    if (rq[0] == 3) fcntl(3, F_SETFD, 0); else dup2(rq[0], 3);   // dup2 onto itself keeps O_CLOEXEC
    setenv("ZENO_MODE", mode.c_str(), 1);
    signal(SIGINT, SIG_DFL);
    execl("/proc/self/exe", "zeno", "--kernel", (char*)nullptr);
    _exit(127);
  }
  close(rq[0]); close(o[1]); close(e[1]);
  req = rq[1]; out = o[0]; err = e[0];
  chain = {pid};
  running = -1;
}

void Kernel::stop() {
  if (pid < 0) return;
  for (int p : chain) kill(p, SIGKILL);
  kill(-pid, SIGKILL);   // whole process group (the kernel calls setpgid)
  close(req); close(out); close(err);
  while (waitpid(-1, nullptr, WNOHANG) > 0) {}
  if (!getenv("ZENO_DIR")) { std::error_code ec; std::filesystem::remove_all("/tmp/zeno-" + std::to_string(pid), ec); }   // the kernel's scratch dir
  pid = -1;
}

void Kernel::interrupt() { if (running > 0) kill(running, SIGKILL); }

J Kernel::call(const std::string& op, const std::string& fields,
               const std::function<void(const std::string&, const std::string&)>& on) {
  if (op == "undo" && chain.size() <= 1) { J j = J::parse("{\"ev\":\"done\",\"ok\":false,\"why\":\"nothing to undo\"}"); return j; }
  std::string body = "{\"op\":" + jq(op) + (fields.empty() ? "" : "," + fields) + "}";
  std::string msg = std::to_string(body.size()) + "\n" + body;
  for (size_t w = 0; w < msg.size();) { ssize_t k = write(req, msg.data() + w, msg.size() - w); if (k <= 0) return J::parse("{\"ev\":\"done\",\"ok\":false,\"why\":\"kernel is gone\"}"); w += k; }
  static const std::string tag = "\x1b]777;";
  std::string buf;
  char tmp[65536];
  for (;;) {
    pollfd p[2] = {{out, POLLIN, 0}, {err, POLLIN, 0}};
    if (poll(p, 2, 500) < 0 && errno != EINTR) break;
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
    if (p[1].revents & POLLIN) { ssize_t k = read(err, tmp, sizeof tmp); if (k > 0) on("err", std::string(tmp, k)); }
    if (!(p[0].revents & (POLLIN | POLLHUP))) continue;
    ssize_t k = read(out, tmp, sizeof tmp);
    if (k <= 0) { pid = -1; return J::parse("{\"ev\":\"done\",\"ok\":false,\"why\":\"kernel died\"}"); }
    buf.append(tmp, k);
    for (;;) {
      size_t f = buf.find(tag);
      size_t textlen = f == std::string::npos ? buf.size() : f;
      if (f == std::string::npos) {   // hold back a possible partial tag or partial UTF-8 sequence
        size_t keep = 0;
        for (size_t n = 1; n < tag.size() && n <= buf.size(); ++n) if (buf.compare(buf.size() - n, n, tag, 0, n) == 0) keep = n;
        size_t u = buf.size() - keep;
        for (size_t b = 1; b <= 3 && b <= u; ++b) {
          unsigned char ch = buf[u - b];
          if ((ch & 0xC0) == 0x80) continue;
          size_t need = ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC0 ? 2 : 1;
          if (need > b) keep = buf.size() - (u - b);
          break;
        }
        textlen = buf.size() - keep;
      }
      if (textlen) { on("out", buf.substr(0, textlen)); buf.erase(0, textlen); }
      if (f == std::string::npos) break;
      size_t e = buf.find('\a');
      if (e == std::string::npos) break;
      std::string json = buf.substr(tag.size(), e - tag.size());
      buf.erase(0, e + 1);
      J ev = J::parse(json);
      std::string kind = ev["ev"].str();
      if (kind == "start") running = (int)ev["pid"].num();
      if (kind == "restored") { int tip = (int)ev["pid"].num(); while (chain.size() > 1 && chain.back() != tip) chain.pop_back(); }
      on("ev", json);
      if (kind == "done") {
        running = -1;
        int tip = (int)ev["pid"].num();
        if (ev["undo"].b || !ev["ok"].b) { while (chain.size() > 1 && chain.back() != tip) chain.pop_back(); }
        else if (tip && chain.back() != tip) chain.push_back(tip);
        while ((int)chain.size() > max_snapshots + 1) { kill(chain.front(), SIGKILL); chain.erase(chain.begin()); }
        return ev;
      }
    }
  }
  return J::parse("{\"ev\":\"done\",\"ok\":false,\"why\":\"poll failed\"}");
}
