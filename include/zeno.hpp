// zeno.hpp — the notebook runtime every cell sees (precompiled).
// Value inspection (types, layout, padding, lazy pointers), display, plotting, benchmarking.
#pragma once
#include <bits/stdc++.h>

namespace zeno {
// ---------------------------------------------------------------- runtime (implemented in the zeno binary)
void emit(std::string_view json);                       // framed event to the frontend
std::string clean_type(std::string_view raw);           // strip cell namespaces, std::__cxx11 ...
bool readable(const void* p, std::size_t n);            // probe memory without faulting
std::string sym_name(const void* p);                    // "fn+off (lib)" for code pointers
std::string heap_info(const void* p, std::size_t* base_off, std::size_t* size); // "" if not a live heap block
[[noreturn]] void fail_current_exception();
void reg_var(const char* name, std::string_view rawtype, const void* addr, std::size_t size,
             void (*fn)(std::string&, const void*, long), int cell);
void here();                                            // gdb backtrace with locals, like a breakpoint
struct counters { double ns = 0, cycles = -1, instructions = -1, cache_misses = -1, branch_misses = -1; };
void perf_begin(); counters perf_end();
int cell_mark(int end);                                 // brackets a cell's own code, excluding dlopen work

// Inspection code is instantiated in every cell; compiling it unoptimized keeps cells fast to build.
#pragma GCC push_options
#pragma GCC optimize("O0")
// ---------------------------------------------------------------- JSON node writer
struct W { std::string s; int depth = 0, max_depth = 4, budget = 4000; };
inline void jstr(std::string& o, std::string_view v) {
  o += '"';
  for (unsigned char c : v) {
    if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
    else if (c == '\n') o += "\\n";
    else if (c == '\t') o += "\\t";
    else if (c < 0x20) { char b[8]; std::snprintf(b, 8, "\\u%04x", c); o += b; }
    else o += char(c);
  }
  o += '"';
}
inline std::string hex(const void* p) { char b[24]; std::snprintf(b, 24, "%p", p); return b; }
template <class T> const char* raw_type() { return __PRETTY_FUNCTION__; }
template <class T> std::string_view type_of() {
  std::string_view p = raw_type<T>();
  auto b = p.find("T = ") + 4;
  return p.substr(b, p.size() - 1 - b);
}
template <class T> std::string num(T v) {
  if constexpr (std::is_floating_point_v<T>) {
    char b[64]; auto r = std::to_chars(b, b + 64, v); return std::string(b, r.ptr);
  } else return std::to_string(v);
}

// ---------------------------------------------------------------- traits
template <class T, template <class...> class Z> struct is_spec : std::false_type {};
template <template <class...> class Z, class... A> struct is_spec<Z<A...>, Z> : std::true_type {};
template <class T, class = void> struct complete : std::false_type {};
template <class T> struct complete<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};
template <class T> concept range = requires(const T& t) { std::begin(t); std::end(t); };
template <class T> concept map_like = range<T> && requires { typename T::key_type; typename T::mapped_type; };
template <class T> concept tuple_like = requires { std::tuple_size<T>::value; };
template <class T> concept streamable = requires(std::ostream& o, const T& t) { o << t; };
template <class T> concept stringish = std::is_convertible_v<const T&, std::string_view> && !std::is_pointer_v<T> && !std::is_array_v<T>;
template <class T> constexpr bool is_char = std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
                                           std::is_same_v<T, unsigned char> || std::is_same_v<T, char8_t>;

// ---------------------------------------------------------------- aggregate reflection (field count, refs, names)
template <class T> struct any_init {
  template <class U> requires(!std::is_base_of_v<U, T>) operator U() const;
};
template <class T, class... A> constexpr std::size_t nfields() {
  if constexpr (sizeof...(A) > 32) return 0;
  else if constexpr (requires { T{A{}..., any_init<T>{}}; }) return nfields<T, A..., any_init<T>>();
  else return sizeof...(A);
}
#define ZENO_T(...) { auto& [__VA_ARGS__] = t; return std::tie(__VA_ARGS__); }
template <std::size_t N, class T> constexpr auto tie(T& t) {
  if constexpr (N == 1) ZENO_T(a) else if constexpr (N == 2) ZENO_T(a, b)
  else if constexpr (N == 3) ZENO_T(a, b, c) else if constexpr (N == 4) ZENO_T(a, b, c, d)
  else if constexpr (N == 5) ZENO_T(a, b, c, d, e) else if constexpr (N == 6) ZENO_T(a, b, c, d, e, f)
  else if constexpr (N == 7) ZENO_T(a, b, c, d, e, f, g) else if constexpr (N == 8) ZENO_T(a, b, c, d, e, f, g, h)
  else if constexpr (N == 9) ZENO_T(a, b, c, d, e, f, g, h, i) else if constexpr (N == 10) ZENO_T(a, b, c, d, e, f, g, h, i, j)
  else if constexpr (N == 11) ZENO_T(a, b, c, d, e, f, g, h, i, j, k) else if constexpr (N == 12) ZENO_T(a, b, c, d, e, f, g, h, i, j, k, l)
  else if constexpr (N == 13) ZENO_T(a, b, c, d, e, f, g, h, i, j, k, l, m) else if constexpr (N == 14) ZENO_T(a, b, c, d, e, f, g, h, i, j, k, l, m, n)
  else if constexpr (N == 15) ZENO_T(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) else if constexpr (N == 16) ZENO_T(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p)
  else return std::tuple<>{};
}
#undef ZENO_T
template <class T> constexpr std::size_t nrefl = std::tuple_size_v<decltype(tie<nfields<T>()>(std::declval<T&>()))>;
template <class T> extern const T fake;
template <class T> struct ref_w { const T& v; };
template <auto P> constexpr const char* pf() { return __PRETTY_FUNCTION__; }
template <class T, std::size_t I> std::string_view field_name() {
  std::string_view p = pf<ref_w{std::get<I>(tie<nfields<T>()>(fake<T>))}>();
  auto e = p.rfind('}'), b = p.find_last_of(":.", e);
  return p.substr(b + 1, e - b - 1);
}
template <class T> concept reflectable = std::is_aggregate_v<T> && std::is_class_v<T> && !std::is_union_v<T> &&
                                       nfields<T>() > 0 && nfields<T>() <= 16
#ifdef ZENO_SAFE
                                       && false
#endif
    ;

// ---------------------------------------------------------------- node rendering
template <class T> void node(W& w, const T& v, std::string_view name = {}, long off = -1);
template <class T> void expand_at(std::string& o, const void* p, long) { W w; node(w, *static_cast<const T*>(p)); o = std::move(w.s); }
template <class T> void expand_arr(std::string& o, const void* p, long n);
template <class T> void expand_more(std::string& o, const void* p, long start);
template <class T> std::string ref(void (*fn)(std::string&, const void*, long), const void* p, long arg = 0) {
  return hex(reinterpret_cast<const void*>(fn)) + ":" + hex(p) + ":" + std::to_string(arg);
}
inline void kv(W& w, const char* k, std::string_view v) { w.s += ",\""; w.s += k; w.s += "\":"; jstr(w.s, v); }
inline void kn(W& w, const char* k, double v) { w.s += ",\""; w.s += k; w.s += "\":"; w.s += std::isfinite(v) ? num(v) : "null"; }

template <class T> void ptr_body(W& w, T* p) {
  kv(w, "k", "ptr");
  if (!p) { kv(w, "v", "nullptr"); return; }
  if constexpr (std::is_function_v<T>) { kv(w, "v", hex((const void*)p) + " " + sym_name((const void*)p)); }
  else {
    std::string v = hex((const void*)p);
    if constexpr (complete<T>::value && !std::is_void_v<T>) {
      if (readable(p, sizeof(T))) {
        if constexpr (is_char<std::remove_cv_t<T>>) {
          std::string s; for (const char* c = (const char*)p; s.size() < 200 && readable(c, 1) && *c; ++c) s += *c;
          std::string q; jstr(q, s); v += " " + q;
        }
        kv(w, "x", ref<T>(&expand_at<std::remove_cv_t<T>>, p));
        std::size_t off = 0, sz = 0;
        std::string h = heap_info(p, &off, &sz);
        if (!h.empty()) {
          kv(w, "h", h);
          if (off == 0 && sz >= 2 * sizeof(T)) kv(w, "xa", ref<T>(&expand_arr<std::remove_cv_t<T>>, p, sz / sizeof(T)));
        }
      } else v += " (unreadable)";
    }
    kv(w, "v", v);
  }
}
template <class R> void seq_body(W& w, const R& r, long start = 0) {
  using E = std::remove_cvref_t<decltype(*std::begin(r))>;
  long n = 0, i = 0;
  if constexpr (requires { std::size(r); }) n = long(std::size(r)); else for (auto it = std::begin(r); it != std::end(r); ++it) ++n;
  kn(w, "n", double(n));
  if constexpr (std::is_arithmetic_v<E> && !std::is_same_v<E, bool>) {
    if (start == 0 && n > 0) {   // stats + downsampled sparkline over the whole range
      double mn = INFINITY, mx = -INFINITY, s = 0, s2 = 0; std::vector<double> sp; long step = std::max(1L, n / 200);
      for (auto& e : r) { double d = double(e); mn = std::min(mn, d); mx = std::max(mx, d); s += d; s2 += d * d; if (i++ % step == 0) sp.push_back(d); }
      double mean = s / n;
      w.s += ",\"st\":{\"min\":" + num(mn) + ",\"max\":" + num(mx) + ",\"mean\":" + num(mean) +
             ",\"std\":" + num(std::sqrt(std::max(0.0, s2 / n - mean * mean))) + "},\"sp\":[";
      for (std::size_t k = 0; k < sp.size(); ++k) w.s += (k ? "," : "") + (std::isfinite(sp[k]) ? num(sp[k]) : std::string("null"));
      w.s += "]";
    }
  }
  if (w.depth > w.max_depth || w.budget <= 0) { kv(w, "x", ref<R>(&expand_at<R>, &r)); return; }
  w.s += ",\"c\":[";
  long lim = start + (w.depth <= 1 ? 100 : 20); i = 0;
  auto it = std::begin(r);
  if constexpr (std::random_access_iterator<decltype(it)>) { it += start; i = start; }
  for (; it != std::end(r) && i < lim; ++it, ++i) {
    if (i < start) continue;
    if (i > start) w.s += ',';
    if constexpr (map_like<R>) {
      const auto& [k, val] = *it;
      if constexpr (std::is_arithmetic_v<std::remove_cvref_t<decltype(k)>>) node(w, val, num(k));
      else if constexpr (stringish<std::remove_cvref_t<decltype(k)>>) { std::string q; jstr(q, std::string_view(k)); node(w, val, q); }
      else node(w, *it, "[" + std::to_string(i) + "]");
    } else node(w, *it, "[" + std::to_string(i) + "]");
  }
  w.s += "]";
  if (i < n) kv(w, "more", ref<R>(&expand_more<R>, &r, i));
}
template <class T> void expand_more(std::string& o, const void* p, long start) {
  W w; w.s = "{\"k\":\"more\""; seq_body(w, *static_cast<const T*>(p), start); w.s += "}"; o = std::move(w.s);
}
template <class T> void expand_arr(std::string& o, const void* p, long n) {
  W w; w.s = "{\"t\":"; jstr(w.s, clean_type(type_of<T>()) + "[" + std::to_string(n) + "]");
  kv(w, "k", "seq"); kn(w, "z", double(n * sizeof(T))); kv(w, "a", hex(p));
  seq_body(w, std::span<const T>(static_cast<const T*>(p), n)); w.s += "}"; o = std::move(w.s);
}
template <class T, std::size_t... I> void rec_body(W& w, const T& v, std::index_sequence<I...>) {
  auto refs = tie<sizeof...(I)>(const_cast<T&>(v));
  long offs[] = {long((const char*)&std::get<I>(refs) - (const char*)&v)...};
  long sizes[] = {long(sizeof(std::get<I>(refs)))...};
  w.s += ",\"c\":[";
  ((w.s += (I ? "," : ""), node(w, std::get<I>(refs), field_name<T, I>(), offs[I])), ...);
  w.s += "],\"p\":[";
  long end = 0; bool first = true;
  auto hole = [&](long at, long n) { if (n > 0) { w.s += (first ? "" : ",") + ("[" + std::to_string(at) + "," + std::to_string(n) + "]"); first = false; } };
  for (std::size_t k = 0; k < sizeof...(I); ++k) { hole(end, offs[k] - end); end = std::max(end, offs[k] + sizes[k]); }
  hole(end, long(sizeof(T)) - end);
  w.s += "]";
}
template <class T> void body(W& w, const T& v) {
  if constexpr (std::is_same_v<T, bool>) { kv(w, "k", "num"); kv(w, "v", v ? "true" : "false"); }
  else if constexpr (is_char<T>) { kv(w, "k", "num"); kv(w, "v", (v >= 32 && v < 127 ? std::string("'") + char(v) + "' " : std::string()) + "(" + std::to_string(int(v)) + ")"); }
  else if constexpr (std::is_arithmetic_v<T>) { kv(w, "k", "num"); kv(w, "v", num(v)); }
  else if constexpr (std::is_enum_v<T>) { kv(w, "k", "num"); kv(w, "v", num(std::underlying_type_t<T>(v))); }
  else if constexpr (std::is_null_pointer_v<T>) { kv(w, "k", "ptr"); kv(w, "v", "nullptr"); }
  else if constexpr (stringish<T> || (std::is_array_v<T> && is_char<std::remove_extent_t<T>>)) {
    std::string_view s(v); kv(w, "k", "str"); kn(w, "n", double(s.size())); kv(w, "v", s.substr(0, 20000));
  }
  else if constexpr (std::is_pointer_v<T>) ptr_body(w, v);
  else if constexpr (is_spec<T, std::unique_ptr>::value) ptr_body(w, v.get());
  else if constexpr (is_spec<T, std::shared_ptr>::value) { ptr_body(w, v.get()); kn(w, "uses", double(v.use_count())); }
  else if constexpr (is_spec<T, std::weak_ptr>::value) { ptr_body(w, v.lock().get()); kn(w, "uses", double(v.use_count())); }
  else if constexpr (is_spec<T, std::optional>::value) {
    kv(w, "k", "opt");
    if (!v) kv(w, "v", "nullopt"); else { w.s += ",\"c\":["; node(w, *v, "value"); w.s += "]"; }
  }
  else if constexpr (is_spec<T, std::variant>::value) {
    kv(w, "k", "opt"); kv(w, "v", "index " + std::to_string(v.index()));
    if (!v.valueless_by_exception()) std::visit([&](const auto& a) { w.s += ",\"c\":["; node(w, a, "alt " + std::to_string(v.index())); w.s += "]"; }, v);
  }
  else if constexpr (is_spec<T, std::chrono::duration>::value) { kv(w, "k", "num"); kv(w, "v", num(std::chrono::duration<double>(v).count()) + " s"); }
  else if constexpr (range<T> && !std::is_same_v<T, std::filesystem::path>) { kv(w, "k", map_like<T> ? "map" : "seq"); seq_body(w, v); }
  else if constexpr (tuple_like<T>) {
    kv(w, "k", "rec"); w.s += ",\"c\":[";
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      ((w.s += (I ? "," : ""), node(w, std::get<I>(v), std::is_same_v<T, std::pair<std::tuple_element_t<0, T>, std::tuple_element_t<1, T>>>
           ? (I ? "second" : "first") : "<" + std::to_string(I) + ">",
           long((const char*)&std::get<I>(v) - (const char*)&v))), ...);
    }(std::make_index_sequence<std::tuple_size_v<T>>{});
    w.s += "]";
  }
  else if constexpr (reflectable<T>) {
    kv(w, "k", "rec");
    if constexpr (streamable<T>) { std::ostringstream os; os << v; kv(w, "v", os.str().substr(0, 2000)); }
    if (w.depth > w.max_depth || w.budget <= 0) kv(w, "x", ref<T>(&expand_at<T>, &v));
    else rec_body(w, v, std::make_index_sequence<nfields<T>()>{});
  }
  else if constexpr (streamable<T>) { std::ostringstream os; os << v; kv(w, "k", "obj"); kv(w, "v", os.str().substr(0, 20000)); }
  else {   // opaque class: raw bytes; the frontend can ask the debugger for its layout
    kv(w, "k", "obj"); kv(w, "rt", type_of<T>());
    std::string b; const unsigned char* p = reinterpret_cast<const unsigned char*>(&v);
    for (std::size_t i = 0; i < std::min<std::size_t>(sizeof(T), 64); ++i) { char x[4]; std::snprintf(x, 4, "%02x", p[i]); b += x; b += (i % 8 == 7 ? "  " : " "); }
    kv(w, "bytes", b);
  }
}
template <class T> void node(W& w, const T& v, std::string_view name, long off) {
  w.s += '{'; --w.budget;
  if (!name.empty()) { w.s += "\"n\":"; jstr(w.s, name); w.s += ','; }
  if (off >= 0) { w.s += "\"o\":" + std::to_string(off) + ','; }
  w.s += "\"t\":"; jstr(w.s, clean_type(type_of<T>()));
  kn(w, "z", double(sizeof(T))); kn(w, "al", double(alignof(T))); kv(w, "a", hex(&v));
  ++w.depth; body(w, v); --w.depth;
  w.s += '}';
}

#pragma GCC pop_options

// ---------------------------------------------------------------- plotting (rendered by the runtime: SVG for web, braille for terminals)
struct series { std::string kind, label; std::vector<double> x, y; };
template <class R> std::vector<double> to_vec(const R& r) { std::vector<double> o; for (auto&& e : r) o.push_back(double(e)); return o; }
struct figure {
  std::vector<series> s; std::string title_, xlabel_, ylabel_; std::vector<std::string> cats;
  std::vector<std::vector<double>> grid; bool logy_ = false; int w = 720, h = 380;
  template <class Y> figure& line(const Y& y, std::string label = {}) { auto v = to_vec(y); std::vector<double> x(v.size()); std::iota(x.begin(), x.end(), 0); s.push_back({"line", label, x, v}); return *this; }
  template <class X, class Y> figure& line(const X& x, const Y& y, std::string label = {}) { s.push_back({"line", label, to_vec(x), to_vec(y)}); return *this; }
  template <class X, class Y> figure& scatter(const X& x, const Y& y, std::string label = {}) { s.push_back({"scatter", label, to_vec(x), to_vec(y)}); return *this; }
  template <class Y> figure& hist(const Y& data, int bins = 30, std::string label = {}) {
    auto v = to_vec(data); if (v.empty()) return *this;
    auto [mn, mx] = std::minmax_element(v.begin(), v.end()); double lo = *mn, hi = *mx == *mn ? *mn + 1 : *mx;
    std::vector<double> x(bins), y(bins);
    for (int i = 0; i < bins; ++i) x[i] = lo + (hi - lo) * (i + 0.5) / bins;
    for (double d : v) y[std::min(bins - 1, int((d - lo) / (hi - lo) * bins))] += 1;
    s.push_back({"bar", label, x, y}); return *this;
  }
  template <class L, class Y> figure& bar(const L& labels, const Y& y, std::string label = {}) {
    cats.clear(); for (auto&& l : labels) { std::ostringstream o; o << l; cats.push_back(o.str()); }
    auto v = to_vec(y); std::vector<double> x(v.size()); std::iota(x.begin(), x.end(), 0); s.push_back({"bar", label, x, v}); return *this;
  }
  template <class M> figure& heatmap(const M& m) { grid.clear(); for (auto&& row : m) grid.push_back(to_vec(row)); return *this; }
  // Setters return a reference on lvalues and a value on temporaries, so `zeno::plot(v).title("t")` stays valid as a cell result.
#define ZENO_SET(name, params, body) \
  figure& name params & { body; return *this; } \
  figure name params && { body; return std::move(*this); }
  ZENO_SET(title, (std::string t), title_ = std::move(t))
  ZENO_SET(xlabel, (std::string t), xlabel_ = std::move(t))
  ZENO_SET(ylabel, (std::string t), ylabel_ = std::move(t))
  ZENO_SET(logy, (bool b = true), logy_ = b)
  ZENO_SET(size, (int W_, int H_), w = W_; h = H_)
#undef ZENO_SET
  void show() const;
};
template <class Y> figure plot(const Y& y) { return figure{}.line(y); }
template <class X, class Y> figure plot(const X& x, const Y& y) { return figure{}.line(x, y); }
template <class X, class Y> figure scatter(const X& x, const Y& y) { return figure{}.scatter(x, y); }
template <class Y> figure hist(const Y& y, int bins = 30) { return figure{}.hist(y, bins); }
template <class L, class Y> figure bar(const L& l, const Y& y) { return figure{}.bar(l, y); }
template <class M> figure heatmap(const M& m) { return figure{}.heatmap(m); }
void html(std::string_view h);
void svg(std::string_view s);

// ---------------------------------------------------------------- data helpers
struct table {
  std::vector<std::string> cols; std::vector<std::vector<std::string>> rows;
  long col_index(std::string_view c) const { for (std::size_t i = 0; i < cols.size(); ++i) if (cols[i] == c) return long(i); throw std::out_of_range("no column " + std::string(c)); }
  std::vector<std::string> col(std::string_view c) const { long i = col_index(c); std::vector<std::string> o; for (auto& r : rows) o.push_back(std::size_t(i) < r.size() ? r[i] : ""); return o; }
  std::vector<double> num(std::string_view c) const { std::vector<double> o; for (auto& s : col(c)) o.push_back(s.empty() ? NAN : std::strtod(s.c_str(), nullptr)); return o; }
  std::size_t size() const { return rows.size(); }
};
table read_csv(const std::string& path, char sep = ',');
template <class T = double> std::vector<T> linspace(T a, T b, std::size_t n) { std::vector<T> v(n); for (std::size_t i = 0; i < n; ++i) v[i] = a + (b - a) * T(i) / T(n > 1 ? n - 1 : 1); return v; }

// ---------------------------------------------------------------- benchmarking
struct bench { std::string name; long iters; double ns_mean, ns_min, ns_std, cycles, instructions, ipc; };
template <class F> bench timeit(F&& f, std::string name = {}, double budget_s = 0.3) {
  auto once = [&] { if constexpr (std::is_void_v<decltype(f())>) f(); else { auto&& r = f(); asm volatile("" : : "r"(&r) : "memory"); } };
  using clk = std::chrono::steady_clock;
  long n = 1;
  for (;;) {   // calibrate the inner loop to ~budget/10
    auto t = clk::now(); for (long i = 0; i < n; ++i) once();
    if (std::chrono::duration<double>(clk::now() - t).count() > budget_s / 10 || n > (1L << 40)) break;
    n *= 4;
  }
  std::vector<double> runs; perf_begin();
  for (int r = 0; r < 7; ++r) { auto t = clk::now(); for (long i = 0; i < n; ++i) once(); runs.push_back(std::chrono::duration<double, std::nano>(clk::now() - t).count() / n); }
  counters c = perf_end();
  double m = std::accumulate(runs.begin(), runs.end(), 0.0) / runs.size(), sd = 0;
  for (double x : runs) sd += (x - m) * (x - m);
  double it = 7.0 * n;
  return {name, n, m, *std::min_element(runs.begin(), runs.end()), std::sqrt(sd / runs.size()),
          c.cycles / it, c.instructions / it, c.cycles > 0 ? c.instructions / c.cycles : NAN};
}
inline std::ostream& operator<<(std::ostream& o, const bench& b) {
  char buf[256];
  std::snprintf(buf, sizeof buf, "%s%.4g ns ± %.2g ns per loop (min %.4g ns; 7 runs × %ld loops)", b.name.empty() ? "" : (b.name + ": ").c_str(), b.ns_mean, b.ns_std, b.ns_min, b.iters);
  o << buf;
  if (b.cycles > 0) { std::snprintf(buf, sizeof buf, "; %.4g cycles, %.4g instr, IPC %.2f", b.cycles, b.instructions, b.ipc); o << buf; }
  return o;
}
// TIMEIT(expr); times expr and prints the result, like IPython's %timeit. It expands to a loop rather than a
// lambda, so it works anywhere (top-level cells, main, any function) and sees locals without capturing them.
// The timer hands out calibrated batches of iterations: grow the batch until it takes budget/10, then 7 runs.
struct timer {
  using clk = std::chrono::steady_clock;
  const char* name; double budget; long n = 1, left = 0; int run = -1; clk::time_point t0 = clk::now(); std::vector<double> runs;
  explicit timer(const char* name, double budget_s = 0.3) : name(name), budget(budget_s) {}
  bool more() { return left-- > 0 || next(); }
  bool next() {
    double dt = std::chrono::duration<double>(clk::now() - t0).count();
    if (run < 0) { if (dt > budget / 10 || n > (1L << 40)) { run = 0; perf_begin(); } else n *= 4; }
    else if (runs.push_back(dt * 1e9 / n), ++run == 7) {
      counters c = perf_end();
      double m = std::accumulate(runs.begin(), runs.end(), 0.0) / 7, sd = 0, it = 7.0 * n;
      for (double x : runs) sd += (x - m) * (x - m);
      std::cout << bench{name, n, m, *std::min_element(runs.begin(), runs.end()), std::sqrt(sd / 7), c.cycles / it, c.instructions / it,
                         c.cycles > 0 ? c.instructions / c.cycles : NAN} << std::endl;
      return false;
    }
    left = n - 1; t0 = clk::now(); return true;   // this call hands out the batch's first iteration
  }
};
struct sink { template <class T> friend void operator,(sink, T&& r) { asm volatile("" : : "r"(&r) : "memory"); } };   // keeps a result alive
#define TIMEIT(...) for (::zeno::timer _zeno_timer(#__VA_ARGS__); _zeno_timer.more();) ::zeno::sink{}, (__VA_ARGS__)

// ---------------------------------------------------------------- cell glue (used by generated code)
struct nothing {};
template <class T> void display(const T& v) {
  if constexpr (std::is_same_v<T, nothing> || std::is_base_of_v<std::ios_base, T>) return;
  else if constexpr (std::is_same_v<T, figure>) v.show();
  else { W w; node(w, v); emit("{\"ev\":\"display\",\"node\":" + w.s + "}"); }
}
int cell_begin(const char* uid);   // marks where a cell's output starts within a batch
// A cell's value is not shown; the frontend gets a reference it can reveal on request.
template <class T> int result(const char* uid, const T& v) {
  if constexpr (std::is_same_v<T, nothing> || std::is_base_of_v<std::ios_base, T>) return 0;
  else if constexpr (std::is_same_v<T, figure>) { v.show(); return 0; }
  else {
    std::string o = "{\"ev\":\"result\",\"uid\":"; jstr(o, uid);
    o += ",\"t\":"; jstr(o, clean_type(type_of<T>()));
    o += ",\"ref\":"; jstr(o, ref<T>(&expand_at<T>, &v));
    emit(o + "}");
    return 0;
  }
}
template <class F> decltype(auto) wrap(F f) {
  if constexpr (std::is_void_v<decltype(f())>) { f(); return nothing{}; } else return f();
}
template <class F> int run(F f) { try { f(); } catch (...) { fail_current_exception(); } return 0; }
template <class T> int reg(const char* name, const T& v, int cell) {
  if constexpr (!std::is_same_v<T, nothing>) reg_var(name, type_of<T>(), &v, sizeof(T), &expand_at<T>, cell);
  return 0;
}
}  // namespace zeno
