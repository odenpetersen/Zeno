// The part of the zeno runtime that lives in the kernel binary (exported to cells via -rdynamic):
// heap tracking (malloc/new interposition), leak scan, memory probes, symbols, plots, perf counters.
#include "../include/zeno.hpp"
#include "common.hpp"
#include "runtime.hpp"
#include <cstring>
#include <sstream>
#include <cxxabi.h>
#include <dlfcn.h>
#include <link.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
void* __libc_malloc(size_t); void __libc_free(void*); void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t); void* __libc_memalign(size_t, size_t);
}

// ================================================================ heap tracking
namespace heap {
struct Blk { uintptr_t p; size_t sz; const void* site; int cell; };
static Blk* tab; static size_t cap, used;
static std::atomic_flag lk = ATOMIC_FLAG_INIT;
static bool on = !getenv("ZENO_NOHEAP");
Stats st;
int cell;
struct Lock { Lock() { while (lk.test_and_set(std::memory_order_acquire)) {} } ~Lock() { lk.clear(std::memory_order_release); } };
static size_t slot(uintptr_t p) { return ((p >> 4) * 0x9E3779B97F4A7C15ull) >> 20 & (cap - 1); }
static void put(const Blk& b) { size_t i = slot(b.p); while (tab[i].p > 1) i = (i + 1) & (cap - 1); if (!tab[i].p) ++used; tab[i] = b; }
static void grow() {
  Blk* old = tab; size_t oc = cap;
  cap = cap ? cap * 2 : 1 << 16;
  tab = (Blk*)mmap(nullptr, cap * sizeof(Blk), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  used = 0;
  for (size_t i = 0; i < oc; ++i) if (old[i].p > 1) put(old[i]);
  if (old) munmap(old, oc * sizeof(Blk));
}
static Blk* find(uintptr_t p) {
  if (!cap) return nullptr;
  for (size_t i = slot(p);; i = (i + 1) & (cap - 1)) { if (!tab[i].p) return nullptr; if (tab[i].p == p) return &tab[i]; }
}
static void add(void* p, size_t sz, const void* site) {
  if (!p || !on) return;
  Lock l;
  if ((used + 1) * 4 > cap * 3) grow();
  put({uintptr_t(p), sz, site, cell});
  st.allocs++; st.bytes += sz; st.live += sz; st.blocks++;
  if (st.live > st.peak) st.peak = st.live;
}
static void del(void* p) {
  if (!p || !on) return;
  Lock l;
  if (Blk* b = find(uintptr_t(p))) { st.frees++; st.live -= b->sz; st.blocks--; b->p = 1; }
}
template <class F> void each(F f) { Lock l; for (size_t i = 0; i < cap; ++i) if (tab[i].p > 1) f(tab[i]); }
static void atfork_prepare() { while (lk.test_and_set(std::memory_order_acquire)) {} }
static void atfork_release() { lk.clear(std::memory_order_release); }
static int registered = pthread_atfork(atfork_prepare, atfork_release, atfork_release);
void enable(bool b) { on = b && !getenv("ZENO_NOHEAP"); }
}  // namespace heap

#define RA __builtin_return_address(0)
extern "C" {
void* malloc(size_t n) { void* p = __libc_malloc(n); heap::add(p, n, RA); return p; }
void free(void* p) { heap::del(p); __libc_free(p); }
void* calloc(size_t a, size_t b) { void* p = __libc_calloc(a, b); heap::add(p, a * b, RA); return p; }
void* realloc(void* p, size_t n) {
  void* q = __libc_realloc(p, n);
  if (q || n == 0) heap::del(p);
  heap::add(q, n, RA);
  return q;
}
void* memalign(size_t al, size_t n) { void* p = __libc_memalign(al, n); heap::add(p, n, RA); return p; }
void* aligned_alloc(size_t al, size_t n) { void* p = __libc_memalign(al, n); heap::add(p, n, RA); return p; }
int posix_memalign(void** out, size_t al, size_t n) { void* p = __libc_memalign(al, n); if (!p) return ENOMEM; heap::add(p, n, RA); *out = p; return 0; }
void* valloc(size_t n) { void* p = __libc_memalign(4096, n); heap::add(p, n, RA); return p; }
}
static void* new_impl(size_t n, size_t al, const void* site) {
  void* p = al > 16 ? __libc_memalign(al, n ? n : 1) : __libc_malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  heap::add(p, n, site); return p;
}
void* operator new(size_t n) { return new_impl(n, 0, RA); }
void* operator new[](size_t n) { return new_impl(n, 0, RA); }
void* operator new(size_t n, std::align_val_t a) { return new_impl(n, size_t(a), RA); }
void* operator new[](size_t n, std::align_val_t a) { return new_impl(n, size_t(a), RA); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return new_impl(n, 0, RA); } catch (...) { return nullptr; } }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return new_impl(n, 0, RA); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { free(p); }

// ================================================================ symbols & memory
namespace zeno {
bool readable(const void* p, std::size_t n) {
  if (!p) return false;
  // Probe one byte per page through the kernel, which reports EFAULT instead of faulting.
  for (uintptr_t a = uintptr_t(p), end = uintptr_t(p) + std::max<size_t>(n, 1); a < end; a = (a | 4095) + 1) {
    char c; iovec l{&c, 1}, r{(void*)a, 1};
    if (syscall(SYS_process_vm_readv, getpid(), &l, 1, &r, 1, 0) != 1) return false;
  }
  return true;
}
std::string demangle(const char* s) {
  int st; char* d = abi::__cxa_demangle(s, nullptr, nullptr, &st);
  std::string r = st == 0 ? d : s; ::free(d); return strip_cell_ns(r);
}
std::string sym_name(const void* p) {
  Dl_info i;
  if (!dladdr(p, &i) || !i.dli_fname) return "?";
  std::string lib = i.dli_fname; lib = lib.substr(lib.rfind('/') + 1);
  if (!i.dli_sname) return "(" + lib + ")";
  long off = (const char*)p - (const char*)i.dli_saddr;
  return demangle(i.dli_sname) + (off ? "+" + std::to_string(off) : "") + " (" + lib + ")";
}
std::string clean_type(std::string_view raw) {
  std::string s = strip_cell_ns(std::string(raw));
  auto rep = [&](std::string_view a, std::string_view b) { for (size_t p; (p = s.find(a)) != std::string::npos;) s.replace(p, a.size(), b); };
  rep("std::__cxx11::", "std::"); rep("std::basic_string<char>", "std::string"); rep("std::basic_string_view<char>", "std::string_view");
  rep("std::basic_ostream<char>", "std::ostream"); rep("zeno::", ""); rep(" >", ">");
  return s;
}
std::string heap_info(const void* p, std::size_t* off, std::size_t* size) {
  heap::Blk b{};
  { heap::Lock l; if (heap::Blk* f = heap::find(uintptr_t(p))) b = *f; }   // no allocation while locked
  if (!b.p) return "";
  *off = 0; *size = b.sz;
  return "heap block, " + std::to_string(b.sz) + " B, allocated in run " + std::to_string(b.cell);
}
[[noreturn]] void fail_current_exception() {
  std::string type = demangle(abi::__cxa_current_exception_type() ? abi::__cxa_current_exception_type()->name() : "?"), what;
  try { throw; } catch (const std::exception& e) { what = e.what(); } catch (...) {}
  emit("{\"ev\":\"error\",\"type\":" + jq(type) + ",\"what\":" + jq(what) + "}");
  _exit(43);
}

// ---------------------------------------------------------------- variable registry (for the variables pane)
struct Var { std::string name, type; const void* addr; size_t size; void (*fn)(std::string&, const void*, long); int cell; };
static std::vector<Var>& vars() { static std::vector<Var> v; return v; }
void reg_var(const char* name, std::string_view rawtype, const void* addr, std::size_t size, void (*fn)(std::string&, const void*, long), int cell) {
  auto& v = vars();
  std::erase_if(v, [&](const Var& x) { return x.name == name; });
  v.push_back({name, clean_type(rawtype), addr, size, fn, cell});
}

// ---------------------------------------------------------------- perf counters
struct Perf {
  int fd[4] = {-1, -1, -1, -1};
  std::chrono::steady_clock::time_point t0;
  void begin() {
    static const uint64_t cfg[4] = {PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_CACHE_MISSES, PERF_COUNT_HW_BRANCH_MISSES};
    for (int i = 0; i < 4; ++i) {
      perf_event_attr a{}; a.size = sizeof a; a.type = PERF_TYPE_HARDWARE; a.config = cfg[i];
      a.disabled = 1; a.exclude_kernel = 1; a.exclude_hv = 1; a.inherit = 1;
      fd[i] = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
      if (fd[i] >= 0) ioctl(fd[i], PERF_EVENT_IOC_ENABLE, 0);
    }
    t0 = std::chrono::steady_clock::now();
  }
  counters end() {
    counters c; c.ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    double* out[4] = {&c.cycles, &c.instructions, &c.cache_misses, &c.branch_misses};
    for (int i = 0; i < 4; ++i) if (fd[i] >= 0) {
      ioctl(fd[i], PERF_EVENT_IOC_DISABLE, 0);
      uint64_t v; if (read(fd[i], &v, 8) == 8) *out[i] = double(v);
      close(fd[i]); fd[i] = -1;
    }
    return c;
  }
};
static Perf bench_perf;
int cell_begin(const char* uid) {
  auto h = rt::heap_stats();
  emit("{\"ev\":\"cell\",\"uid\":" + jq(uid) + ",\"t\":" + num(rt::now_ms()) + ",\"allocs\":" + std::to_string(h.allocs) + ",\"bytes\":" + std::to_string(h.bytes) + "}");
  return 0;
}
static Perf cell_perf;
int cell_mark(int end) {
  auto& m = rt::mark;
  if (!end) { m.begun = true; m.h0 = rt::heap_stats(); m.t0 = rt::now_ms(); cell_perf.begin(); }
  else { m.c = cell_perf.end(); m.t1 = rt::now_ms(); m.h1 = rt::heap_stats(); m.ended = true; }
  return 0;
}
void perf_begin() { bench_perf.begin(); }
counters perf_end() { return bench_perf.end(); }

// ---------------------------------------------------------------- gdb snapshot of the live stack
void here() {
  std::string gdb = rt::gdb_bt(getpid(), "zeno::here");
  emit("{\"ev\":\"rich\",\"mime\":\"text/x-stack\",\"data\":" + jq(gdb) + "}");
}

// ---------------------------------------------------------------- rich output & plots
void html(std::string_view h) { emit("{\"ev\":\"rich\",\"mime\":\"text/html\",\"data\":" + jq(h) + "}"); }
void svg(std::string_view s) { emit("{\"ev\":\"rich\",\"mime\":\"image/svg+xml\",\"data\":" + jq(s) + "}"); }

namespace {
struct Ticks { double lo, hi, step; };
Ticks nice(double lo, double hi, int n = 5) {
  if (!(hi > lo)) { lo -= 1; hi += 1; }
  double raw = (hi - lo) / n, mag = std::pow(10, std::floor(std::log10(raw))), f = raw / mag;
  double step = (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * mag;
  return {std::floor(lo / step) * step, std::ceil(hi / step) * step, step};
}
std::string fmt(double v) { char b[32]; std::snprintf(b, 32, "%.6g", std::fabs(v) < 1e-12 ? 0.0 : v); return b; }
std::string esc(const std::string& s) { std::string o; for (char c : s) o += c == '<' ? "&lt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : std::string(1, c); return o; }
const char* kColors[] = {"#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"};

std::string render_svg(const figure& f) {
  int W = f.w, H = f.h, L = 64, R = 20, T = f.title_.empty() ? 16 : 40, B = f.xlabel_.empty() ? 36 : 54;
  std::ostringstream o;
  o << "<svg xmlns='http://www.w3.org/2000/svg' class='zeno-plot' viewBox='0 0 " << W << " " << H << "' width='" << W << "' height='" << H
    << "' font-family='system-ui,sans-serif' font-size='12'>";
  if (!f.title_.empty()) o << "<text x='" << L << "' y='24' font-size='15' font-weight='600' fill='currentColor'>" << esc(f.title_) << "</text>";
  if (!f.grid.empty()) {   // heatmap: single-hue sequential ramp
    size_t rows = f.grid.size(), cols = 0; double mn = INFINITY, mx = -INFINITY;
    for (auto& r : f.grid) { cols = std::max(cols, r.size()); for (double v : r) if (std::isfinite(v)) { mn = std::min(mn, v); mx = std::max(mx, v); } }
    double cw = double(W - L - R) / std::max<size_t>(cols, 1), ch = double(H - T - B) / rows;
    for (size_t i = 0; i < rows; ++i) for (size_t j = 0; j < f.grid[i].size(); ++j) {
      double t = mx > mn ? (f.grid[i][j] - mn) / (mx - mn) : 0.5;
      int r = int(232 - t * (232 - 16)), g = int(240 - t * (240 - 70)), b = int(252 - t * (252 - 148));
      o << "<rect x='" << L + j * cw << "' y='" << T + i * ch << "' width='" << cw + 0.5 << "' height='" << ch + 0.5 << "' fill='rgb(" << r << "," << g << "," << b
        << ")'><title>[" << i << "][" << j << "] = " << fmt(f.grid[i][j]) << "</title></rect>";
    }
    o << "<text x='" << L << "' y='" << H - 10 << "' fill='currentColor' opacity='.7'>" << rows << "×" << cols << "  min " << fmt(mn) << "  max " << fmt(mx) << "</text></svg>";
    return o.str();
  }
  double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
  bool bars = false;
  for (auto& s : f.s) {
    if (s.kind == "bar") { bars = true; y0 = std::min(y0, 0.0); }
    for (double x : s.x) if (std::isfinite(x)) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
    for (double y : s.y) if (std::isfinite(y) && (!f.logy_ || y > 0)) { double v = f.logy_ ? std::log10(y) : y; y0 = std::min(y0, v); y1 = std::max(y1, v); }
  }
  if (!std::isfinite(x0)) { x0 = 0; x1 = 1; y0 = 0; y1 = 1; }
  double bw = 0;
  if (bars) for (auto& s : f.s) if (s.kind == "bar" && s.x.size() > 1) bw = (s.x.back() - s.x.front()) / (s.x.size() - 1);
  if (bars) { if (bw <= 0) bw = 1; x0 -= bw / 2; x1 += bw / 2; }
  Ticks tx = nice(x0, x1), ty = nice(y0, y1);
  if (bars) { tx.lo = x0; tx.hi = x1; }
  auto X = [&](double x) { return L + (x - tx.lo) / (tx.hi - tx.lo) * (W - L - R); };
  auto Y = [&](double y) { return H - B - ((f.logy_ ? std::log10(y) : y) - ty.lo) / (ty.hi - ty.lo) * (H - T - B); };
  for (double y = ty.lo; y <= ty.hi + ty.step / 2; y += ty.step) {
    double py = H - B - (y - ty.lo) / (ty.hi - ty.lo) * (H - T - B);
    o << "<line x1='" << L << "' x2='" << W - R << "' y1='" << py << "' y2='" << py << "' stroke='currentColor' stroke-opacity='.12'/>"
      << "<text x='" << L - 8 << "' y='" << py + 4 << "' text-anchor='end' fill='currentColor' fill-opacity='.65'>" << (f.logy_ ? "1e" + fmt(y) : fmt(y)) << "</text>";
  }
  if (!f.cats.empty()) {
    size_t every = std::max<size_t>(1, f.cats.size() / 12);
    for (size_t i = 0; i < f.cats.size(); i += every)
      o << "<text x='" << X(double(i)) << "' y='" << H - B + 18 << "' text-anchor='middle' fill='currentColor' fill-opacity='.65'>" << esc(f.cats[i].substr(0, 14)) << "</text>";
  } else
    for (double x = tx.lo + (bars ? 0 : 0); x <= tx.hi + tx.step / 2; x += tx.step)
      if (x >= x0 - 1e-9 || !bars)
        o << "<text x='" << X(x) << "' y='" << H - B + 18 << "' text-anchor='middle' fill='currentColor' fill-opacity='.65'>" << fmt(x) << "</text>";
  o << "<line x1='" << L << "' x2='" << W - R << "' y1='" << H - B << "' y2='" << H - B << "' stroke='currentColor' stroke-opacity='.4'/>";
  if (!f.xlabel_.empty()) o << "<text x='" << (L + W - R) / 2 << "' y='" << H - 12 << "' text-anchor='middle' fill='currentColor'>" << esc(f.xlabel_) << "</text>";
  if (!f.ylabel_.empty()) o << "<text transform='translate(14," << (T + H - B) / 2 << ") rotate(-90)' text-anchor='middle' fill='currentColor'>" << esc(f.ylabel_) << "</text>";
  int nb = 0; for (auto& s : f.s) nb += s.kind == "bar";
  int bi = 0;
  for (size_t k = 0; k < f.s.size(); ++k) {
    auto& s = f.s[k]; std::string c = "var(--s" + std::to_string(k % 8 + 1) + "," + kColors[k % 8] + ")";
    size_t n = std::min(s.x.size(), s.y.size());
    if (s.kind == "line") {
      o << "<path fill='none' stroke='" << c << "' stroke-width='2' stroke-linejoin='round' d='";
      bool pen = false;
      for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(s.y[i]) || (f.logy_ && s.y[i] <= 0)) { pen = false; continue; }
        o << (pen ? "L" : "M") << X(s.x[i]) << "," << Y(s.y[i]) << " "; pen = true;
      }
      o << "'/>";
      if (n <= 60) for (size_t i = 0; i < n; ++i) if (std::isfinite(s.y[i]))
        o << "<circle cx='" << X(s.x[i]) << "' cy='" << Y(s.y[i]) << "' r='6' fill='transparent'><title>" << fmt(s.x[i]) << ", " << fmt(s.y[i]) << "</title></circle>";
    } else if (s.kind == "scatter") {
      for (size_t i = 0; i < n; ++i) if (std::isfinite(s.y[i]))
        o << "<circle cx='" << X(s.x[i]) << "' cy='" << Y(s.y[i]) << "' r='4' fill='" << c << "' fill-opacity='.75' stroke='var(--surface,#fff)' stroke-width='1'>"
          << (n <= 2000 ? "<title>" + fmt(s.x[i]) + ", " + fmt(s.y[i]) + "</title>" : "") << "</circle>";
    } else {
      double w = std::max(1.0, (X(bw) - X(0)) / nb - 2);
      for (size_t i = 0; i < n; ++i) {
        double px = X(s.x[i]) - (X(bw) - X(0)) / 2 + 1 + bi * (w + 2) / 1.0, py = Y(std::max(0.0, s.y[i])), base = Y(std::min(0.0, s.y[i]));
        if (f.logy_) { py = Y(std::max(1e-300, s.y[i])); base = H - B; }
        o << "<rect x='" << px << "' y='" << py << "' width='" << w << "' height='" << std::max(0.0, base - py) << "' rx='2' fill='" << c << "'><title>"
          << (f.cats.size() > i ? esc(f.cats[i]) : fmt(s.x[i])) << ": " << fmt(s.y[i]) << "</title></rect>";
      }
      ++bi;
    }
  }
  if (f.s.size() >= 2) {
    double lx = L + 8;
    for (size_t k = 0; k < f.s.size(); ++k) {
      std::string lab = f.s[k].label.empty() ? "series " + std::to_string(k + 1) : f.s[k].label;
      o << "<rect x='" << lx << "' y='" << T - 12 << "' width='10' height='10' rx='2' fill='var(--s" << k % 8 + 1 << "," << kColors[k % 8] << ")'/>"
        << "<text x='" << lx + 14 << "' y='" << T - 3 << "' fill='currentColor'>" << esc(lab) << "</text>";
      lx += 30 + 7 * lab.size();
    }
  }
  o << "</svg>";
  return o.str();
}

// Terminal rendering: braille dots (2x4 per character cell).
std::string render_braille(const figure& f) {
  const int cw = 64, chh = 16, PW = cw * 2, PH = chh * 4;
  std::vector<std::vector<int>> grid(chh, std::vector<int>(cw, 0)), owner(chh, std::vector<int>(cw, -1));
  double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
  std::vector<series> ss = f.s;
  if (!f.grid.empty()) {
    static const char* shades[] = {" ", "░", "▒", "▓", "█"};
    double mn = INFINITY, mx = -INFINITY;
    for (auto& r : f.grid) for (double v : r) if (std::isfinite(v)) { mn = std::min(mn, v); mx = std::max(mx, v); }
    std::string o = f.title_.empty() ? "" : f.title_ + "\n";
    for (auto& r : f.grid) { for (size_t j = 0; j < std::min<size_t>(r.size(), 120); ++j) { int k = int((r[j] - mn) / (mx - mn + 1e-300) * 4.999); o += shades[std::clamp(k, 0, 4)]; } o += "\n"; }
    return o + "min " + fmt(mn) + "  max " + fmt(mx) + "\n";
  }
  for (auto& s : ss) {
    for (double x : s.x) if (std::isfinite(x)) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
    for (double y : s.y) if (std::isfinite(y)) { y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (s.kind == "bar") y0 = std::min(y0, 0.0);
  }
  if (!std::isfinite(x0)) return "(empty plot)\n";
  if (x1 == x0) x1 = x0 + 1;
  if (y1 == y0) y1 = y0 + 1;
  auto dot = [&](int px, int py, int k) {
    if (px < 0 || px >= PW || py < 0 || py >= PH) return;
    static const int bit[4][2] = {{0x1, 0x8}, {0x2, 0x10}, {0x4, 0x20}, {0x40, 0x80}};
    grid[py / 4][px / 2] |= bit[py % 4][px % 2]; owner[py / 4][px / 2] = k;
  };
  auto PX = [&](double x) { return int(std::lround((x - x0) / (x1 - x0) * (PW - 1))); };
  auto PY = [&](double y) { return int(std::lround((y1 - y) / (y1 - y0) * (PH - 1))); };
  for (size_t k = 0; k < ss.size(); ++k) {
    auto& s = ss[k]; size_t n = std::min(s.x.size(), s.y.size());
    for (size_t i = 0; i < n; ++i) {
      if (!std::isfinite(s.y[i])) continue;
      int px = PX(s.x[i]), py = PY(s.y[i]);
      if (s.kind == "bar") { for (int y = py; y <= PY(0); ++y) { dot(px, y, k); dot(px + 1, y, k); } }
      else if (s.kind == "line" && i > 0 && std::isfinite(s.y[i - 1])) {
        int ax = PX(s.x[i - 1]), ay = PY(s.y[i - 1]), steps = std::max(std::abs(px - ax), std::abs(py - ay)) + 1;
        for (int t = 0; t <= steps; ++t) dot(ax + (px - ax) * t / steps, ay + (py - ay) * t / steps, k);
      } else dot(px, py, k);
    }
  }
  static const char* ansi[] = {"\x1b[34m", "\x1b[33m", "\x1b[36m", "\x1b[93m", "\x1b[35m", "\x1b[32m", "\x1b[94m", "\x1b[31m"};
  std::string o = f.title_.empty() ? "" : "  \x1b[1m" + f.title_ + "\x1b[0m\n";
  for (int r = 0; r < chh; ++r) {
    char lab[16]; std::snprintf(lab, sizeof lab, "%9s ", r == 0 ? fmt(y1).c_str() : r == chh - 1 ? fmt(y0).c_str() : "");
    o += std::string("\x1b[2m") + lab + "┤\x1b[0m";
    for (int c = 0; c < cw; ++c) {
      if (!grid[r][c]) { o += ' '; continue; }
      int cp = 0x2800 + grid[r][c];
      char u[4] = {char(0xE0 | cp >> 12), char(0x80 | ((cp >> 6) & 63)), char(0x80 | (cp & 63)), 0};
      o += std::string(ansi[owner[r][c] % 8]) + u + "\x1b[0m";
    }
    o += "\n";
  }
  std::string axis, xs = fmt(x0), xe = fmt(x1);
  for (int i = 0; i < cw; ++i) axis += "─";
  o += "\x1b[2m          └" + axis + "\n           " + xs + std::string(std::max<int>(1, cw - (int)xs.size() - (int)xe.size()), ' ') + xe + "\x1b[0m\n";
  if (ss.size() >= 2) for (size_t k = 0; k < ss.size(); ++k) o += std::string("           ") + ansi[k % 8] + "■\x1b[0m " + (ss[k].label.empty() ? "series " + std::to_string(k + 1) : ss[k].label) + "\n";
  return o;
}
}  // namespace

void bench::show() const {
  if (!rt::web) { std::cout << *this << std::endl; return; }
  std::ostringstream line; line << *this;
  auto jnum = [](double x) { char b[32]; std::snprintf(b, sizeof b, "%.6g", x); return std::isfinite(x) ? std::string(b) : std::string("null"); };
  std::string rs; for (double x : runs) rs += (rs.empty() ? "" : ",") + jnum(x);
  emit("{\"ev\":\"rich\",\"mime\":\"application/x-zeno-bench\",\"text\":" + jq(line.str()) + ",\"data\":{\"name\":" + jq(name) +
       ",\"iters\":" + std::to_string(iters) + ",\"mean\":" + jnum(ns_mean) + ",\"std\":" + jnum(ns_std) + ",\"min\":" + jnum(ns_min) +
       ",\"cycles\":" + jnum(cycles) + ",\"instr\":" + jnum(instructions) + ",\"ipc\":" + jnum(ipc) + ",\"runs\":[" + rs + "]}}");
}
void figure::show() const {
  if (rt::web) svg(render_svg(*this));
  else emit("{\"ev\":\"rich\",\"mime\":\"text/x-ansi\",\"data\":" + jq(render_braille(*this)) + "}");
}

table read_csv(const std::string& path, char sep) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  table t; std::string line; bool header = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> row; std::string cur; bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (q) { if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; } else if (c == '"') q = false; else cur += c; }
      else if (c == '"') q = true; else if (c == sep) { row.push_back(cur); cur.clear(); } else cur += c;
    }
    row.push_back(cur);
    if (header) { t.cols = row; header = false; } else t.rows.push_back(row);
  }
  return t;
}
}  // namespace zeno

// ================================================================ kernel-side helpers
namespace rt {
bool web = false;
Mark mark;
double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
Perf_ perf_begin() { auto* p = new zeno::Perf; p->begin(); return p; }
zeno::counters perf_end(Perf_ p) { auto c = static_cast<zeno::Perf*>(p)->end(); delete static_cast<zeno::Perf*>(p); return c; }
heap::Stats heap_stats() { heap::Lock l; return heap::st; }
void heap_cell_begin(int cell) { heap::Lock l; heap::cell = cell; heap::st.peak = heap::st.live; }

std::string vars_json() {
  std::string o = "[";
  for (auto& v : zeno::vars()) {
    std::string node;
    if (zeno::readable(v.addr, std::min<size_t>(v.size, 4096))) v.fn(node, v.addr, 0);
    else node = "{\"t\":" + jq(v.type) + ",\"v\":\"(unreadable)\"}";
    o += (o.size() > 1 ? "," : "") + std::string("{\"name\":") + jq(v.name) + ",\"cell\":" + std::to_string(v.cell) + ",\"node\":" + node + "}";
  }
  return o + "]";
}

// Resolve code addresses to "function at cellN.cpp:line" (addr2line for cell libraries, dladdr otherwise).
std::vector<std::string> resolve(const std::vector<const void*>& sites) {
  std::vector<std::string> out(sites.size());
  std::map<std::string, std::vector<size_t>> by_lib;
  for (size_t i = 0; i < sites.size(); ++i) {
    Dl_info d;
    if (dladdr(sites[i], &d) && d.dli_fname && std::string(d.dli_fname).ends_with(".so") && std::string(d.dli_fname).find("/c") != std::string::npos &&
        std::string(d.dli_fname).find("zeno-") != std::string::npos)
      by_lib[d.dli_fname].push_back(i);
    else out[i] = zeno::sym_name(sites[i]);
  }
  for (auto& [lib, idx] : by_lib) {
    Dl_info d; dladdr(sites[idx[0]], &d);
    std::vector<std::string> argv = {"addr2line", "-C", "-f", "-e", lib};
    for (size_t i : idx) { char b[32]; std::snprintf(b, 32, "0x%lx", (unsigned long)((const char*)sites[i] - (const char*)d.dli_fbase - 1)); argv.push_back(b); }
    Proc p = run_cmd(argv);
    std::istringstream in(p.out); std::string fn, loc;
    for (size_t i : idx) {
      std::getline(in, fn); std::getline(in, loc);
      loc = loc.substr(0, loc.find(' '));
      out[i] = strip_cell_ns(fn) + " at " + loc.substr(loc.rfind('/') + 1);
      if (out[i].find("lambda()") != std::string::npos || fn == "??") out[i] = loc.substr(loc.rfind('/') + 1);
    }
  }
  return out;
}

// Allocations made by zeno itself (or the dynamic loader) are bookkeeping, not the notebook's memory.
bool internal_site(const void* site) {
  static const void* self = [] { Dl_info d{}; dladdr((void*)&heap_json, &d); return d.dli_fbase; }();
  Dl_info d;
  if (!dladdr(site, &d)) return false;
  return d.dli_fbase == self || (d.dli_fname && std::strstr(d.dli_fname, "ld-linux"));
}

std::string heap_json() {
  struct Site { size_t bytes = 0, n = 0; };
  std::map<const void*, Site> sites; std::map<int, size_t> cells;
  heap::enable(false);   // runs in a throwaway fork; also keeps us from allocating under the lock
  std::vector<heap::Blk> blocks;
  heap::each([&](const heap::Blk& b) { blocks.push_back(b); });
  std::erase_if(blocks, [](const heap::Blk& b) { return b.cell == 0 || internal_site(b.site); });
  for (auto& b : blocks) { auto& s = sites[b.site]; s.bytes += b.sz; s.n++; cells[b.cell] += b.sz; }
  std::vector<std::pair<const void*, Site>> top(sites.begin(), sites.end());
  std::sort(top.begin(), top.end(), [](auto& a, auto& b) { return a.second.bytes > b.second.bytes; });
  if (top.size() > 25) top.resize(25);
  std::vector<const void*> addrs; for (auto& t : top) addrs.push_back(t.first);
  auto names = resolve(addrs);
  auto s = heap::st;
  size_t live = 0; for (auto& b : blocks) live += b.sz;
  std::string o = "{\"live\":" + std::to_string(live) + ",\"blocks\":" + std::to_string(blocks.size()) + ",\"allocs\":" + std::to_string(s.allocs) +
                  ",\"frees\":" + std::to_string(s.frees) + ",\"total\":" + std::to_string(s.bytes) + ",\"sites\":[";
  for (size_t i = 0; i < top.size(); ++i)
    o += (i ? "," : "") + std::string("{\"site\":") + jq(names[i]) + ",\"bytes\":" + std::to_string(top[i].second.bytes) + ",\"n\":" + std::to_string(top[i].second.n) + "}";
  o += "],\"cells\":[";
  bool first = true;
  for (auto& [c, b] : cells) { o += (first ? "" : ",") + std::string("[") + std::to_string(c) + "," + std::to_string(b) + "]"; first = false; }
  // address-ordered map of blocks (downsampled) for the heap picture
  std::sort(blocks.begin(), blocks.end(), [](auto& a, auto& b) { return a.p < b.p; });
  o += "],\"map\":[";
  size_t step = std::max<size_t>(1, blocks.size() / 4000);
  for (size_t i = 0; i < blocks.size(); i += step)
    o += (i ? "," : "") + std::string("[") + std::to_string(blocks[i].p) + "," + std::to_string(blocks[i].sz) + "," + std::to_string(blocks[i].cell) + "]";
  return o + "]}";
}

// Valgrind-style leak check: blocks not reachable from any loaded object's data segments.
std::string leaks_json() {
  heap::enable(false);
  std::vector<heap::Blk> b;
  heap::each([&](const heap::Blk& x) { b.push_back(x); });
  std::sort(b.begin(), b.end(), [](auto& x, auto& y) { return x.p < y.p; });
  std::vector<char> mark(b.size());
  std::vector<size_t> work;
  auto scan = [&](uintptr_t lo, uintptr_t hi) {
    for (uintptr_t a = (lo + 7) & ~7ul; a + 8 <= hi; a += 8) {
      uintptr_t v = *(const uintptr_t*)a;
      auto it = std::upper_bound(b.begin(), b.end(), v, [](uintptr_t v, const heap::Blk& x) { return v < x.p; });
      if (it == b.begin()) continue;
      --it;
      size_t i = it - b.begin();
      if (v < it->p + std::max<size_t>(it->sz, 1) && !mark[i]) { mark[i] = 1; work.push_back(i); }
    }
  };
  struct Seg { uintptr_t lo, hi; };
  std::vector<Seg> segs;
  dl_iterate_phdr([](dl_phdr_info* i, size_t, void* d) {
    for (int k = 0; k < i->dlpi_phnum; ++k) {
      auto& ph = i->dlpi_phdr[k];
      if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W)) static_cast<std::vector<Seg>*>(d)->push_back({i->dlpi_addr + ph.p_vaddr, i->dlpi_addr + ph.p_vaddr + ph.p_memsz});
    }
    return 0;
  }, &segs);
  for (auto& s : segs) if (zeno::readable((void*)s.lo, 1)) scan(s.lo, s.hi);
  while (!work.empty()) { size_t i = work.back(); work.pop_back(); scan(b[i].p, b[i].p + b[i].sz); }
  std::map<const void*, std::pair<size_t, size_t>> lost;
  size_t total = 0, n = 0;
  for (size_t i = 0; i < b.size(); ++i) if (!mark[i] && b[i].cell != 0 && !internal_site(b[i].site)) { auto& l = lost[b[i].site]; l.first += b[i].sz; l.second++; total += b[i].sz; ++n; }
  std::vector<std::pair<const void*, std::pair<size_t, size_t>>> v(lost.begin(), lost.end());
  std::sort(v.begin(), v.end(), [](auto& a, auto& c) { return a.second.first > c.second.first; });
  if (v.size() > 25) v.resize(25);
  std::vector<const void*> addrs; for (auto& x : v) addrs.push_back(x.first);
  auto names = resolve(addrs);
  std::string o = "{\"bytes\":" + std::to_string(total) + ",\"blocks\":" + std::to_string(n) + ",\"sites\":[";
  for (size_t i = 0; i < v.size(); ++i)
    o += (i ? "," : "") + std::string("{\"site\":") + jq(names[i]) + ",\"bytes\":" + std::to_string(v[i].second.first) + ",\"n\":" + std::to_string(v[i].second.second) + "}";
  return o + "]}";
}

// Attach gdb to `pid` and return a cleaned backtrace with locals, starting below `after` (a frame substring).
std::string gdb_bt(int pid, const char* after) {
  std::string p = std::to_string(pid);
  Proc r = run_cmd({"gdb", "-p", p, "-batch", "-nx", "-q", "-ex", "set print pretty off", "-ex", "set print frame-arguments scalars",
                    "-ex", "set print elements 16", "-ex", "bt full 40"});
  std::istringstream in(r.out); std::string line, o; bool go = !after;
  int frames = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] != '#') { if (go && line.rfind("        ", 0) == 0 && frames) o += strip_cell_ns(line) + "\n"; continue; }
    if (!go) { if (line.find(after) != std::string::npos) go = true; continue; }
    if (line.find("__start_context") != std::string::npos || line.find("_dl_init") != std::string::npos || line.find("call_init") != std::string::npos) break;
    std::string l = strip_cell_ns(line);
    for (auto pat : {"_FUN()", "__static_initialization_and_destruction", "zeno::run<", "_GLOBAL__sub_I"})
      if (l.find(pat) != std::string::npos) { l.clear(); break; }
    static const std::regex lam(R"(\{lambda\(\)#\d+\}::operator\(\)\(\) const)");
    l = std::regex_replace(l, lam, "<cell>");
    if (l.empty()) { frames = 0; continue; }
    o += l + "\n"; ++frames;
  }
  if (o.empty()) o = r.status == 0 ? r.out : "gdb unavailable: " + r.out;
  return o;
}
}  // namespace rt
