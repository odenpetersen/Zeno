// The kernel: compiles cells to shared libraries and runs them in a chain of forked processes.
//
// The "tip" process holds the current state. To run a cell it forks; the child dlopen()s the
// cell and, if it succeeds, becomes the new tip while the parent stays blocked in waitpid() as a
// copy-on-write snapshot. A crash, exception or interrupt kills only the child, so the parent
// resumes with the state from before the cell. Undo is the tip exiting on purpose.
//
// Protocol: requests arrive on fd 3 as "<length>\n<json>". Everything flows back on stdout:
// user output as plain bytes, events as ESC ] 777 ; <json> BEL.
#include "common.hpp"
#include "runtime.hpp"
#include <csignal>
#include <poll.h>
#include <sys/syscall.h>
#include <filesystem>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

void zeno::emit(std::string_view json) {
  std::cout.flush(); std::cerr.flush(); fflush(stdout); fflush(stderr);
  std::string f = "\x1b]777;" + std::string(json) + "\a";
  for (size_t w = 0; w < f.size();) { ssize_t k = write(1, f.data() + w, f.size() - w); if (k <= 0) _exit(0); w += k; }
}
using zeno::emit;

namespace {
struct State {
  std::string prelude, header, cflags, ldflags;
  std::vector<std::string> path;   // the nested cell namespaces the header opens
  int last = 0;
} st;
std::string dir, cxx = getenv("ZENO_CXX") ? getenv("ZENO_CXX") : "g++";
bool is_root = true;
const std::string kBase = "-std=c++20 -O2 -g -march=native -fPIC -fdiagnostics-color=always -Wno-unused-result";

std::vector<std::string> words(const std::string& s) { std::istringstream in(s); std::vector<std::string> v; for (std::string w; in >> w;) v.push_back(w); return v; }
std::string ftos(double d) { char b[32]; std::snprintf(b, 32, "%.6g", d); return b; }
std::string hexs(size_t v) { char b[32]; std::snprintf(b, 32, "%zx", v); return b; }

std::string header_path() {
  if (const char* h = getenv("ZENO_HOME")) return std::string(h) + "/include/zeno.hpp";
  for (auto p : {self_dir() + "/include/zeno.hpp", self_dir() + "/../include/zeno.hpp", self_dir() + "/../share/zeno/zeno.hpp"})
    if (access(p.c_str(), R_OK) == 0) return p;
  return "zeno.hpp";
}

// Precompiled header per flag set, cached in ~/.cache/zeno.
std::string ensure_pch(const std::string& flags) {
  std::string hdr = read_file(header_path());
  size_t h = std::hash<std::string>{}(hdr + cxx + flags + run_cmd({cxx, "-dumpfullversion"}).out);
  const char* xdg = getenv("XDG_CACHE_HOME");
  std::string cache = (xdg ? std::string(xdg) : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.cache") + "/zeno";
  std::string d = cache + "/pch-" + hexs(h);
  if (access((d + "/zeno.hpp.gch").c_str(), R_OK) == 0) return d;
  emit("{\"ev\":\"status\",\"msg\":\"building precompiled header (one time, ~5s)\"}");
  std::error_code ec;   // drop caches built from older versions of the runtime header
  for (auto& e : std::filesystem::directory_iterator(cache, ec))
    if (e.path().filename().string().rfind("pch-", 0) == 0 && read_file((e.path() / "zeno.hpp").string()) != hdr) std::filesystem::remove_all(e.path(), ec);
  run_cmd({"mkdir", "-p", d});
  write_file(d + "/zeno.hpp", hdr);
  auto argv = words(cxx + " " + flags);
  for (auto a : {"-x", "c++-header"}) argv.push_back(a);
  argv.push_back(d + "/zeno.hpp"); argv.push_back("-o"); argv.push_back(d + "/zeno.hpp.gch.tmp");
  Proc p = run_cmd(argv);
  if (p.status == 0) rename((d + "/zeno.hpp.gch.tmp").c_str(), (d + "/zeno.hpp.gch").c_str());
  else emit("{\"ev\":\"status\",\"msg\":" + jq("precompiled header failed:\n" + p.out) + "}");
  return d;
}

std::string clean_diag(std::string s) {
  for (size_t p; (p = s.find(dir + "/")) != std::string::npos;) s.erase(p, dir.size() + 1);
  return strip_cell_ns(s);
}

// ---------------------------------------------------------------- crash reporting (async-signal-safe-ish)
char g_pid[24], g_crash_buf[1 << 16], g_json[1 << 17];
char alt_stack[1 << 16];
void w(const char* s) { (void)!write(1, s, strlen(s)); }
void on_crash(int sig, siginfo_t* si, void*) {
  int p[2]; size_t n = 0;
  if (access("/usr/bin/gdb", X_OK) == 0 && !getenv("ZENO_NOGDB") && pipe(p) == 0) {
    pid_t g = vfork();
    if (g == 0) {
      dup2(p[1], 1); dup2(p[1], 2);
      const char* av[] = {"gdb", "-p", g_pid, "-batch", "-nx", "-q", "-ex", "set print frame-arguments scalars", "-ex", "bt full 30", nullptr};
      execv("/usr/bin/gdb", (char**)av); _exit(127);
    }
    close(p[1]);
    for (ssize_t k; n < sizeof g_crash_buf - 1 && (k = read(p[0], g_crash_buf + n, sizeof g_crash_buf - 1 - n)) > 0;) n += k;
    close(p[0]);
    if (g > 0) waitpid(g, nullptr, 0);
  }
  g_crash_buf[n] = 0;
  char* bt = strstr(g_crash_buf, "<signal handler called>");
  bt = bt ? strchr(bt, '\n') : nullptr;
  bt = bt ? bt + 1 : g_crash_buf + n;
  char* end = nullptr;   // cut at the first frame that belongs to the loader / kernel
  for (const char* m : {"__static_initialization_and_destruction", "_GLOBAL__sub_I", "in call_init", "_dl_init", "__start_context"}) {
    char* e = strstr(bt, m);
    if (e && (!end || e < end)) end = e;
  }
  if (end) { while (end > bt && end[-1] != '\n') --end; *end = 0; }
  size_t j = 0;
  for (char* c = bt; *c && j < sizeof g_json - 8; ++c) {   // strip __cN:: and JSON-escape
    if (c[0] == '_' && c[1] == '_' && c[2] == 'c' && c[3] >= '0' && c[3] <= '9') {
      char* e = c + 3; while ((*e >= '0' && *e <= '9') || *e == '_') ++e;
      if (e[0] == ':' && e[1] == ':') { c = e + 1; continue; }
    }
    if (*c == '"' || *c == '\\') { g_json[j++] = '\\'; g_json[j++] = *c; }
    else if (*c == '\n') { g_json[j++] = '\\'; g_json[j++] = 'n'; }
    else if ((unsigned char)*c >= 0x20) g_json[j++] = *c;
  }
  g_json[j] = 0;
  char head[160];
  snprintf(head, sizeof head, "\x1b]777;{\"ev\":\"crash\",\"sig\":\"%s\",\"addr\":\"%p\",\"bt\":\"", sigabbrev_np(sig), si->si_addr);
  fflush(stdout);
  w(head); w(g_json); w("\"}\a");
  signal(sig, SIG_DFL); raise(sig);
}
void install_crash_handlers() {
  snprintf(g_pid, sizeof g_pid, "%d", getpid());
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
  stack_t ss{}; ss.ss_sp = alt_stack; ss.ss_size = sizeof alt_stack; sigaltstack(&ss, nullptr);
  struct sigaction sa{}; sa.sa_sigaction = on_crash; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
  for (int s : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(s, &sa, nullptr);
  std::set_terminate([] {
    if (std::current_exception()) zeno::fail_current_exception();
    emit("{\"ev\":\"error\",\"type\":\"std::terminate\",\"what\":\"terminate called without an active exception\"}");
    _exit(43);
  });
}

// ---------------------------------------------------------------- run a function on a fresh, measurable stack
ucontext_t main_ctx, cell_ctx;
std::function<void()>* cell_fn;
void trampoline() { (*cell_fn)(); }
size_t run_on_fresh_stack(std::function<void()> f) {
  const size_t sz = 512ul << 20, pg = 4096;   // 512 MB of virtual stack; only touched pages cost memory
  char* base = (char*)mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
  if (base == MAP_FAILED) { f(); return 0; }
  mprotect(base, pg, PROT_NONE);
  cell_fn = &f;
  getcontext(&cell_ctx);
  cell_ctx.uc_stack.ss_sp = base; cell_ctx.uc_stack.ss_size = sz; cell_ctx.uc_link = &main_ctx;
  makecontext(&cell_ctx, trampoline, 0);
  swapcontext(&main_ctx, &cell_ctx);
  std::vector<unsigned char> res(sz / pg);
  mincore(base, sz, res.data());
  size_t lowest = res.size();
  for (size_t i = 1; i < res.size(); ++i) if (res[i] & 1) { lowest = i; break; }
  munmap(base, sz);
  return (res.size() - lowest) * pg;
}

long rss_kb() { long a = 0, b = 0; FILE* f = fopen("/proc/self/statm", "r"); if (f) { if (fscanf(f, "%ld %ld", &a, &b) != 2) b = 0; fclose(f); } return b * 4; }

// Wait for a child, but if the frontend dies meanwhile (its end of the request pipe closes), take the whole
// kernel down with it: otherwise a long-running cell would keep going as an orphan.
//
// A stop request (SIGTERM to this process) is passed on to a running batch, which gets a second to acknowledge
// it through `ack` (by checking zeno::interrupted() or reaching its next statement cell); otherwise it's killed.
volatile sig_atomic_t g_stop = 0;
bool g_grace_expired = false;
void wait_child(pid_t pid, int& status, int ack = -1) {
  int pfd = (int)syscall(SYS_pidfd_open, pid, 0);   // readable when the child exits; else poll every 200 ms
  bool asked = false, acked = false;
  double deadline = 0;
  for (;;) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid || (r < 0 && errno != EINTR)) break;
    if (g_stop && !asked) { asked = true; g_stop = 0; if (ack < 0) kill(pid, SIGKILL); else { kill(pid, SIGTERM); deadline = rt::now_ms() + 1000; } }
    bool waiting = asked && !acked && ack >= 0;
    if (waiting && rt::now_ms() > deadline) { kill(pid, SIGKILL); g_grace_expired = true; waiting = false; acked = true; }
    pollfd p[3] = {{3, 0, 0}, {pfd, POLLIN, 0}, {waiting ? ack : -1, POLLIN, 0}};
    int timeout = waiting ? std::max(1, (int)(deadline - rt::now_ms())) : pfd >= 0 ? -1 : 200;
    if (poll(p, 3, timeout) > 0) {
      if (p[0].revents & (POLLHUP | POLLERR | POLLNVAL)) kill(0, SIGKILL);
      if (p[2].revents) acked = true;
    }
  }
  if (pfd >= 0) close(pfd);
}

// Run `f` in a throwaway fork so wild pointers can't take down the state.
void in_fork(const std::function<void()>& f) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) {
    for (int s : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) signal(s, SIG_DFL);
    f(); fflush(stdout); _exit(0);
  }
  int status; wait_child(pid, status);
  if (!WIFEXITED(status) || WEXITSTATUS(status)) emit("{\"ev\":\"error\",\"type\":\"inspection\",\"what\":\"inspection crashed (memory not readable as that type)\"}");
}

// Disassembly of a batch's library; each instruction carries [file index or -1, line, text].
std::string asm_json(int id, const std::vector<std::string>& files) {
  std::string so = dir + "/c" + std::to_string(id) + ".so";
  Proc p = run_cmd({"objdump", "-d", "-l", "-C", "--no-show-raw-insn", "-M", "intel", "-j", ".text", so});
  std::istringstream in(p.out);
  std::string line, o = "[", fn, body; int cur = -1, cf = -1; bool mine = false;
  auto flush = [&] { if (!fn.empty() && mine) o += (o.size() > 1 ? "," : "") + std::string("{\"fn\":") + jq(strip_cell_ns(fn)) + ",\"ins\":[" + body + "]}"; fn.clear(); body.clear(); mine = false; };
  while (std::getline(in, line)) {
    if (line.size() > 18 && line[16] == ' ' && line[17] == '<' && line.back() == ':') { flush(); fn = line.substr(18, line.size() - 20); cur = -1; continue; }
    if (fn.empty() || line.empty()) continue;
    if (line[0] != ' ') {   // "file:line (discriminator n)" or "func():" location marker from -l
      if (line.back() == ':' && line.find("()") != std::string::npos) continue;
      std::string loc = line.substr(0, line.find(' '));
      size_t c = loc.rfind(':');
      if (c == std::string::npos) continue;
      cf = -1; cur = std::atoi(loc.c_str() + c + 1);
      for (size_t i = 0; i < files.size(); ++i) if (loc.compare(0, c, files[i]) == 0) cf = (int)i;
      continue;
    }
    if (cf >= 0) mine = true;
    std::string ins = line; for (auto& ch : ins) if (ch == '\t') ch = ' ';
    ins = strip_cell_ns(ins.substr(ins.find_first_not_of(' ')));
    body += (body.empty() ? "" : ",") + std::string("[") + std::to_string(cf) + "," + std::to_string(cur) + "," + jq(ins) + "]";
  }
  flush();
  std::string fl;
  for (auto& f : files) fl += (fl.empty() ? "" : ",") + jq(f);
  return "{\"files\":[" + fl + "],\"fns\":" + o + "]}";
}
std::map<int, std::vector<std::string>> batch_files;   // exec id -> source files, for asm

void done(const std::string& extra) { emit("{\"ev\":\"done\",\"pid\":" + std::to_string(getpid()) + ",\"last\":" + std::to_string(st.last) + extra + "}"); }

void op_exec(const J& r) {
  int id = (int)r["id"].num();
  std::vector<Src> cells;
  std::vector<std::string> files;
  for (auto& c : r["cells"].a) {
    Src s{c["uid"].str(), c["code"].str(), c["file"].str(), (int)c["line"].num(1)};
    if (s.file.empty()) { s.file = dir + "/" + s.uid + ".cpp"; write_file(s.file, s.code); }   // REPL input
    if (std::find(files.begin(), files.end(), s.file) == files.end()) files.push_back(s.file);
    cells.push_back(s);
  }
  batch_files[id] = files;
  Gen g = gen_batch(cells, id);
  std::string tu = "// generated by zeno, batch " + std::to_string(id) + "\n" + st.prelude + g.pre + "\n" + st.header +
                   "\n" + g.body + "\n" + std::string(st.path.size(), '}') + "\n";
  std::string cpp = dir + "/c" + std::to_string(id) + ".cpp", so = dir + "/c" + std::to_string(id) + ".so";
  write_file(cpp, tu);
  std::string pchflags = kBase + " " + st.cflags + " " + g.cflags;
  std::string pch = ensure_pch(pchflags);
  auto argv = words(cxx + " " + pchflags + " " + g.flags);
  for (auto a : {std::string("-include"), pch + "/zeno.hpp", std::string("-I."), std::string("-shared"), std::string("-o"), so, cpp}) argv.push_back(a);
  for (auto& l : words(st.ldflags + " " + g.ldflags)) argv.push_back(l);
  Proc p = run_cmd(argv);
  if (p.status != 0 && p.out.find("zeno.hpp") != std::string::npos) {   // reflection failed on some type: retry without it
    argv.insert(argv.begin() + 1, "-DZENO_SAFE");
    Proc q = run_cmd(argv);
    if (q.status == 0) { q.ms += p.ms; p = q; }
  }
  bool ok = p.status == 0;
  emit("{\"ev\":\"compile\",\"ok\":" + std::string(ok ? "true" : "false") + ",\"ms\":" + ftos(p.ms) + ",\"diag\":" + jq(clean_diag(p.out)) + "}");
  if (!ok) return done(",\"ok\":false");
  if (r["preview"].b) {   // types and layouts of every cell's value, read from debug info; nothing runs
    std::string prefix; for (auto& n : st.path) prefix += n + "::";
    std::vector<std::string> argv = {"gdb", "-batch", "-nx", "-q", so};
    for (auto& v : g.values) {
      std::string q = prefix; for (size_t i = 0; i < v.ns_count; ++i) q += g.ns[i] + "::";
      q += v.name;
      for (auto c : {"echo @@" + v.uid + "\\n", "ptype /o '" + q + "'"}) { argv.push_back("-ex"); argv.push_back(c); }
    }
    Proc gp = run_cmd(argv);
    std::string o; size_t at = 0;
    while ((at = gp.out.find("@@", at)) != std::string::npos) {
      size_t nl = gp.out.find('\n', at), nx = gp.out.find("@@", nl);
      std::string uid = gp.out.substr(at + 2, nl - at - 2), text = gp.out.substr(nl + 1, (nx == std::string::npos ? gp.out.size() : nx) - nl - 1);
      if (text.find("type = ") != std::string::npos && text.find("No symbol") == std::string::npos)
        o += (o.empty() ? "" : ",") + jq(uid) + ":" + jq(strip_cell_ns(text.substr(text.find("type = ") == 0 ? 0 : 0)));
      at = nx == std::string::npos ? gp.out.size() : nx;
    }
    emit("{\"ev\":\"preview\",\"types\":{" + o + "}}");
    return done(",\"ok\":true");
  }
  if (g.want_asm) emit("{\"ev\":\"asm\",\"asm\":" + asm_json(id, files) + "}");

  int okp[2], ack[2]; if (pipe(okp)) return done(",\"ok\":false");
  if (pipe(ack)) ack[0] = ack[1] = -1;
  fflush(stdout);
  g_stop = 0; g_grace_expired = false;
  pid_t pid = fork();
  if (pid == 0) {
    close(okp[0]); close(ack[0]);
    is_root = false;
    rt::stop_flag = 0; rt::stop_fd = ack[1];
    struct sigaction term{}; term.sa_handler = [](int) { rt::stop_flag = 1; }; sigaction(SIGTERM, &term, nullptr);
    install_crash_handlers();
    emit("{\"ev\":\"start\",\"pid\":" + std::to_string(getpid()) + "}");
    rt::mark = {};
    heap::Stats h0 = rt::heap_stats();
    rt::heap_cell_begin(id);
    rusage u0, u1; getrusage(RUSAGE_SELF, &u0);
    long rss0 = rss_kb();
    auto perf = rt::perf_begin();
    double t0 = rt::now_ms();
    void* handle = nullptr; std::string err;
    size_t stack = run_on_fresh_stack([&] {
      try { handle = dlopen(so.c_str(), RTLD_NOW | RTLD_GLOBAL); if (!handle) err = dlerror(); }
      catch (...) { zeno::fail_current_exception(); }
    });
    double t1 = rt::now_ms();
    zeno::counters c = rt::perf_end(perf);
    getrusage(RUSAGE_SELF, &u1);
    heap::Stats h1 = rt::heap_stats();
    double load_ms = t1 - t0;
    if (!handle) {
      static const std::regex sym("undefined symbol: (\\S+)");
      std::smatch m;
      if (std::regex_search(err, m, sym)) err += "\n  = " + zeno::demangle(m[1].str().c_str()) + "  (declared but never defined?)";
      emit("{\"ev\":\"error\",\"type\":\"load\",\"what\":" + jq(clean_diag(err)) + "}");
      _exit(43);
    }
    if (rt::mark.ended) { h0 = rt::mark.h0; h1 = rt::mark.h1; c = rt::mark.c; t0 = rt::mark.t0; t1 = rt::mark.t1; }
    st.prelude += g.pre;
    st.header += g.header;
    for (auto& n : g.ns) st.path.push_back(n);
    st.cflags += g.cflags; st.ldflags += g.ldflags; st.last = id;
    auto tv = [](timeval a, timeval b) { return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_usec - a.tv_usec) / 1e3; };
    emit("{\"ev\":\"stats\",\"compile_ms\":" + ftos(p.ms) + ",\"run_ms\":" + ftos(t1 - t0) + ",\"load_ms\":" + ftos(load_ms) + ",\"user_ms\":" + ftos(tv(u0.ru_utime, u1.ru_utime)) +
         ",\"sys_ms\":" + ftos(tv(u0.ru_stime, u1.ru_stime)) + ",\"cycles\":" + ftos(c.cycles) + ",\"instructions\":" + ftos(c.instructions) +
         ",\"cache_misses\":" + ftos(c.cache_misses) + ",\"branch_misses\":" + ftos(c.branch_misses) + ",\"minflt\":" + std::to_string(u1.ru_minflt - u0.ru_minflt) +
         ",\"rss_kb\":" + std::to_string(rss_kb()) + ",\"rss_delta_kb\":" + std::to_string(rss_kb() - rss0) +
         ",\"allocs\":" + std::to_string(h1.allocs - h0.allocs) + ",\"frees\":" + std::to_string(h1.frees - h0.frees) +
         ",\"alloc_bytes\":" + std::to_string(h1.bytes - h0.bytes) + ",\"heap_net\":" + std::to_string((long)h1.live - (long)h0.live) +
         ",\"heap_peak\":" + std::to_string(h1.peak - h0.live) + ",\"heap_live\":" + std::to_string(h1.live) + ",\"stack\":" + std::to_string(stack) + "}");
    std::string stopped = rt::stop_report();   // also resets, since this process now becomes the tip
    if (!stopped.empty()) emit(stopped);
    if (rt::stop_fd >= 0) close(rt::stop_fd);
    rt::stop_fd = -1;
    struct sigaction tip{}; tip.sa_handler = [](int) { g_stop = 1; }; sigaction(SIGTERM, &tip, nullptr);
    (void)!write(okp[1], "1", 1); close(okp[1]);
    return done(",\"ok\":true");
  }
  close(okp[1]); close(ack[1]);
  int status = 0;
  wait_child(pid, status, ack[0]);
  close(ack[0]);
  char b; bool committed = read(okp[0], &b, 1) == 1;
  close(okp[0]);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 42) return done(",\"ok\":true,\"undo\":true");
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && committed) return;   // frontend went away: cascade down the chain
  std::string why = WIFSIGNALED(status) ? (WTERMSIG(status) == SIGKILL ? (g_grace_expired ? "stopped: it didn't check zeno::interrupted() within a second, so the run was discarded" : "interrupted") : std::string("killed by ") + sigabbrev_np(WTERMSIG(status)))
                    : WEXITSTATUS(status) == 43 ? "exception" : "exit(" + std::to_string(WEXITSTATUS(status)) + ") called";
  if (!committed) return done(",\"ok\":false,\"why\":" + jq(why));
  emit("{\"ev\":\"restored\",\"why\":" + jq(why) + ",\"pid\":" + std::to_string(getpid()) + ",\"last\":" + std::to_string(st.last) + "}");
}

std::string layout(const std::string& type) {
  std::vector<std::string> argv = {"gdb", "-p", std::to_string(getpid()), "-batch", "-nx", "-q"};
  std::vector<std::string> cands = {type};
  std::string prefix;
  for (auto& n : st.path) { prefix += n + "::"; cands.insert(cands.begin(), prefix + type); }
  for (auto& c : cands) { argv.push_back("-ex"); argv.push_back("ptype /o " + c); }
  Proc p = run_cmd(argv);
  size_t a = p.out.find("/* offset");
  if (a == std::string::npos) return "no layout found for " + type + " (gdb: " + p.out.substr(p.out.size() > 600 ? p.out.size() - 600 : 0) + ")";
  size_t e = p.out.find("total size", a); e = p.out.find("}", e); e = p.out.find('\n', e);
  return strip_cell_ns(p.out.substr(a, e - a));
}

// ---------------------------------------------------------------- preprocessor macros
std::string pp_input(const std::string& pre) { return "#include \"" + ensure_pch(kBase + " " + st.cflags) + "/zeno.hpp\"\n" + st.prelude + "\n" + pre + "\n"; }
std::string macro_names(const std::string& pre) {
  static std::map<std::string, std::string> cache;
  std::string in = pp_input(pre);
  if (cache.count(in)) return cache[in];
  auto argv = words(cxx + " " + kBase + " " + st.cflags); argv.insert(argv.end(), {"-E", "-dM", "-x", "c++", "-"});
  Proc p = run_cmd(argv, in);
  std::istringstream ls(p.out); std::string o, l;
  while (std::getline(ls, l)) {
    if (l.rfind("#define ", 0) != 0) continue;
    size_t e = l.find_first_of(" (", 8);
    std::string name = l.substr(8, e - 8);
    if (name.rfind("__", 0) == 0 || name.rfind("_GLIBCXX", 0) == 0 || name.rfind("_ZENO", 0) == 0) continue;   // implementation noise
    o += (o.empty() ? "" : ",") + std::string("[") + jq(name) + "," + (e < l.size() && l[e] == '(' ? "1" : "0") + "]";
  }
  return cache[in] = "[" + o + "]";
}
// Expand one macro invocation (the identifier at `at`, plus its argument list if function-like).
std::string expand_macro(const std::string& code, size_t at, const std::string& pre) {
  size_t e = at;
  while (e < code.size() && (std::isalnum((unsigned char)code[e]) || code[e] == '_')) ++e;
  size_t k = e; while (k < code.size() && std::isspace((unsigned char)code[k])) ++k;
  if (k < code.size() && code[k] == '(') {
    int d = 0;
    for (; k < code.size(); ++k) { d += code[k] == '(' ? 1 : code[k] == ')' ? -1 : 0; if (!d) { e = k + 1; break; } }
  }
  auto argv = words(cxx + " " + kBase + " " + st.cflags); argv.insert(argv.end(), {"-E", "-P", "-x", "c++", "-"});
  Proc p = run_cmd(argv, pp_input(pre) + "__ZENO_B__ " + code.substr(at, e - at) + " __ZENO_E__\n");
  size_t b = p.out.rfind("__ZENO_B__"), x = p.out.rfind("__ZENO_E__");
  std::string text = b != std::string::npos && x > b ? p.out.substr(b + 10, x - b - 10) : "/* expansion failed */";
  text.erase(0, text.find_first_not_of(" \n")); text.erase(text.find_last_not_of(" \n") + 1);
  return "{\"b\":" + std::to_string(at) + ",\"e\":" + std::to_string(e) + ",\"text\":" + jq(text) + "}";
}

bool read_request(J& out) {
  std::string len; char c;
  for (;;) { ssize_t k = read(3, &c, 1); if (k <= 0) return false; if (c == '\n') break; len += c; }
  std::string s(std::stoul(len), '\0');
  for (size_t got = 0; got < s.size();) { ssize_t k = read(3, s.data() + got, s.size() - got); if (k <= 0) return false; got += k; }
  out = J::parse(s);
  return true;
}
}  // namespace

int kernel_main() {
  setpgid(0, 0);
  struct sigaction st{}; st.sa_handler = [](int) { g_stop = 1; }; sigaction(SIGTERM, &st, nullptr);   // no SA_RESTART: wakes the wait
  signal(SIGCHLD, SIG_DFL);
  signal(SIGPIPE, SIG_DFL);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  rt::web = getenv("ZENO_MODE") && std::string(getenv("ZENO_MODE")) == "web";
  dir = getenv("ZENO_DIR") ? getenv("ZENO_DIR") : "/tmp/zeno-" + std::to_string(getpid());
  mkdir(dir.c_str(), 0700);
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
  for (J r; read_request(r);) {
    std::string op = r["op"].str();
    if (op == "exec") op_exec(r);
    else if (op == "expand") {
      void *fn = nullptr, *addr = nullptr; long arg = 0; Dl_info d;
      if (sscanf(r["ref"].str().c_str(), "%p:%p:%ld", &fn, &addr, &arg) == 3 && dladdr(fn, &d) && d.dli_saddr == fn)
        in_fork([&] { std::string o; reinterpret_cast<void (*)(std::string&, const void*, long)>(fn)(o, addr, arg); emit("{\"ev\":\"node\",\"node\":" + o + "}"); });
      else emit("{\"ev\":\"error\",\"type\":\"expand\",\"what\":\"stale reference\"}");
      done("");
    }
    else if (op == "vars") { in_fork([] { emit("{\"ev\":\"vars\",\"vars\":" + rt::vars_json() + "}"); }); done(""); }
    else if (op == "heap") { in_fork([] { emit("{\"ev\":\"heap\",\"heap\":" + rt::heap_json() + "}"); }); done(""); }
    else if (op == "leaks") { in_fork([] { emit("{\"ev\":\"leaks\",\"leaks\":" + rt::leaks_json() + "}"); }); done(""); }
    else if (op == "asm") { int id = (int)r["id"].num(); emit("{\"ev\":\"asm\",\"asm\":" + asm_json(id, batch_files[id]) + "}"); done(""); }
    else if (op == "layout") { emit("{\"ev\":\"layout\",\"text\":" + jq(layout(r["type"].str())) + "}"); done(""); }
    else if (op == "macros") { emit("{\"ev\":\"macros\",\"names\":" + macro_names(r["pre"].str()) + "}"); done(""); }
    else if (op == "macro") { emit("{\"ev\":\"macro\",\"m\":" + expand_macro(r["code"].str(), (size_t)r["at"].num(), r["pre"].str()) + "}"); done(""); }
    else if (op == "undo") { if (is_root) done(",\"ok\":false,\"why\":\"nothing to undo\""); else { fflush(stdout); _exit(42); } }
    else if (op == "source") { emit("{\"ev\":\"source\",\"text\":" + jq(read_file(dir + "/c" + std::to_string((int)r["id"].num()) + ".cpp")) + "}"); done(""); }
    else done(",\"ok\":false,\"why\":\"unknown op\"");
  }
  return 0;
}
