// Terminal frontend: REPL with a small line editor, plus batch "run" mode.
#include "common.hpp"
#include <csignal>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace {
const char *DIM = "\x1b[2m", *RST = "\x1b[0m", *RED = "\x1b[31m", *GRN = "\x1b[32m", *YEL = "\x1b[33m", *CYN = "\x1b[36m", *BLD = "\x1b[1m", *MAG = "\x1b[35m";
Kernel* g_kernel;
std::vector<std::string> g_refs;
std::vector<std::string> g_history;   // REPL inputs that ran

std::string human(double b) {
  const char* u[] = {"B", "KB", "MB", "GB", "TB"}; int i = 0; double v = std::fabs(b);
  while (v >= 1024 && i < 4) { v /= 1024; b /= 1024; ++i; }
  char s[32]; std::snprintf(s, 32, i ? "%.1f %s" : "%.0f %s", b, u[i]); return s;
}
std::string si(double v) {
  char s[32];
  if (v >= 1e9) std::snprintf(s, 32, "%.2fG", v / 1e9); else if (v >= 1e6) std::snprintf(s, 32, "%.2fM", v / 1e6);
  else if (v >= 1e3) std::snprintf(s, 32, "%.1fk", v / 1e3); else std::snprintf(s, 32, "%.0f", v);
  return s;
}
std::string mkref(const std::string& r, const char* what = "▸") { g_refs.push_back(r); return std::string(CYN) + " [" + what + " #" + std::to_string(g_refs.size()) + "]" + RST; }
std::string spark(const J& sp) {
  static const char* b[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  double mn = 1e300, mx = -1e300;
  for (auto& v : sp.a) if (v.t == J::Num) { mn = std::min(mn, v.n); mx = std::max(mx, v.n); }
  std::string o; size_t step = std::max<size_t>(1, sp.a.size() / 60);
  for (size_t i = 0; i < sp.a.size(); i += step) o += sp.a[i].t == J::Num ? b[mx > mn ? int((sp.a[i].n - mn) / (mx - mn) * 7.99) : 3] : " ";
  return o;
}
bool scalar(const J& n) { std::string k = n["k"].str(); return (k == "num" || k == "str" || k == "ptr") && !n.has("c"); }
std::string val(const J& n) {
  std::string k = n["k"].str(), v = n["v"].str();
  if (k == "str") return std::string(GRN) + jq(v) + RST;
  if (k == "num") return std::string(YEL) + v + RST;
  if (k == "ptr") {
    std::string o = std::string(MAG) + v + RST;
    if (n.has("h")) o += std::string(DIM) + " (" + n["h"].str() + ")" + RST;
    if (n.has("x")) o += mkref(n["x"].str(), "*");
    if (n.has("xa")) o += mkref(n["xa"].str(), "[]");
    if (n.has("uses")) o += std::string(DIM) + " use_count=" + std::to_string((int)n["uses"].num()) + RST;
    return o;
  }
  return v;
}

void render(std::ostream& o, const J& n, const std::string& ind, const std::string& name) {
  std::string k = n["k"].str(), t = n["t"].str();
  std::string head = ind + (name.empty() ? "" : std::string(BLD) + name + RST + " ");
  std::string ty = std::string(DIM) + t + RST;
  if (scalar(n)) { o << head << ty << " = " << val(n) << "\n"; return; }
  if (k == "seq" || k == "map" || k == "more") {
    o << head << ty << DIM << " size " << (long)n["n"].num() << RST;
    if (n.has("st")) {
      auto& s = n["st"];
      o << "  " << CYN << spark(n["sp"]) << RST << DIM << "  min " << s["min"].num() << " max " << s["max"].num() << " mean " << s["mean"].num() << " std " << s["std"].num() << RST;
    }
    if (n.has("x") && !n.has("c")) o << mkref(n["x"].str());
    o << "\n";
    auto& c = n["c"].a;
    bool flat = k == "seq" && !c.empty() && std::all_of(c.begin(), c.end(), [](const J& e) { return scalar(e) && e["k"].str() != "ptr"; });
    if (flat) {
      std::string line = ind + "  [";
      for (size_t i = 0; i < c.size(); ++i) { line += (i ? ", " : "") + val(c[i]); }
      o << line << (n.has("more") ? std::string(", …") + mkref(n["more"].str(), "more") : "") << "]\n";
      return;
    }
    for (auto& e : c) render(o, e, ind + "  ", e["n"].str());
    if (n.has("more")) o << ind << "  …" << mkref(n["more"].str(), "more") << "\n";
    return;
  }
  if (k == "rec") {
    o << head << ty << DIM << "  (" << (long)n["z"].num() << " B, align " << (long)n["al"].num() << ")" << RST << (n.has("v") ? "  " + n["v"].str() : "") << "\n";
    if (n.has("x") && !n.has("c")) { o << ind << "  " << mkref(n["x"].str()) << "\n"; return; }
    std::vector<std::pair<long, std::string>> rows;
    for (auto& e : n["c"].a) {
      std::ostringstream sub;
      if (scalar(e)) sub << ind << "  " << DIM << "+" << std::setw(3) << (long)e["o"].num() << " " << std::setw(3) << (long)e["z"].num() << "B" << RST << "  " << BLD << e["n"].str() << RST << " " << DIM << e["t"].str() << RST << " = " << val(e) << "\n";
      else { sub << ind << "  " << DIM << "+" << std::setw(3) << (long)e["o"].num() << " " << std::setw(3) << (long)e["z"].num() << "B" << RST << "\n"; render(sub, e, ind + "    ", e["n"].str()); }
      rows.emplace_back((long)e["o"].num(), sub.str());
    }
    for (auto& p : n["p"].a) {
      std::ostringstream sub;
      sub << ind << "  " << DIM << "+" << std::setw(3) << (long)p[0].num() << " " << std::setw(3) << (long)p[1].num() << "B  ░░ padding" << RST << "\n";
      rows.emplace_back((long)p[0].num(), sub.str());
    }
    std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& r : rows) o << r.second;
    return;
  }
  if (k == "opt") { o << head << ty << " " << n["v"].str() << "\n"; for (auto& e : n["c"].a) render(o, e, ind + "  ", e["n"].str()); return; }
  o << head << ty << " = " << n["v"].str();
  if (n.has("bytes")) o << "\n" << ind << "  " << DIM << n["bytes"].str() << RST << "  (use .layout " << t << ")";
  o << "\n";
}

void print_heap(const J& h, bool leaks) {
  if (leaks) std::cout << (h["bytes"].num() > 0 ? RED : GRN) << "definitely lost: " << human(h["bytes"].num()) << " in " << (long)h["blocks"].num() << " blocks" << RST << "\n";
  else {
    std::cout << BLD << "heap: " << human(h["live"].num()) << " live in " << (long)h["blocks"].num() << " blocks" << RST << DIM << "  (" << (long)h["allocs"].num()
              << " allocs, " << (long)h["frees"].num() << " frees, " << human(h["total"].num()) << " allocated in total)" << RST << "\n";
    std::cout << DIM << "live bytes by cell:" << RST;
    for (auto& c : h["cells"].a) std::cout << "  [" << (long)c[0].num() << "] " << human(c[1].num());
    std::cout << "\n";
  }
  for (auto& s : h["sites"].a) std::cout << "  " << std::setw(10) << human(s["bytes"].num()) << std::setw(8) << (long)s["n"].num() << "×  " << s["site"].str() << "\n";
}

// ---------------------------------------------------------------- line editor
struct Editor {
  std::vector<std::string> hist;
  termios orig{};
  bool tty = isatty(0) && isatty(1);
  bool read(const std::string& prompt, std::string& out) {
    if (!tty) { std::cout << prompt << std::flush; return bool(std::getline(std::cin, out)); }
    tcgetattr(0, &orig);
    termios raw = orig; raw.c_lflag &= ~(ICANON | ECHO | ISIG); raw.c_iflag &= ~(IXON | ICRNL); raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &raw);
    std::string buf; size_t pos = 0, h = hist.size(); bool ok = true;
    auto draw = [&] {
      size_t col = prompt.size() - count_esc(prompt) + pos;
      std::cout << "\r\x1b[K" << prompt << buf << "\r";
      if (col) std::cout << "\x1b[" << col << "C";
      std::cout << std::flush;
    };
    draw();
    for (;;) {
      char c; if (::read(0, &c, 1) != 1) { ok = false; break; }
      if (c == '\r' || c == '\n') break;
      if (c == 3) { buf.clear(); std::cout << "^C"; pos = 0; ok = true; out = "\x03"; tcsetattr(0, TCSAFLUSH, &orig); std::cout << "\n"; return true; }
      if (c == 4) { if (buf.empty()) { ok = false; break; } if (pos < buf.size()) buf.erase(pos, 1); }
      else if (c == 127 || c == 8) { if (pos) buf.erase(--pos, 1); }
      else if (c == 1) pos = 0;
      else if (c == 5) pos = buf.size();
      else if (c == 11) buf.erase(pos);
      else if (c == 21) { buf.erase(0, pos); pos = 0; }
      else if (c == 12) std::cout << "\x1b[H\x1b[2J";
      else if (c == 23) { size_t p = pos; while (p && buf[p - 1] == ' ') --p; while (p && buf[p - 1] != ' ') --p; buf.erase(p, pos - p); pos = p; }
      else if (c == '\t') { buf.insert(pos, "  "); pos += 2; }
      else if (c == 27) {
        char s[3] = {0, 0, 0};
        if (::read(0, s, 1) != 1 || ::read(0, s + 1, 1) != 1) continue;
        if (s[1] >= '0' && s[1] <= '9') { if (::read(0, s + 2, 1) != 1) continue; if (s[1] == '3' && pos < buf.size()) buf.erase(pos, 1); if (s[1] == '1' || s[1] == '7') pos = 0; if (s[1] == '4' || s[1] == '8') pos = buf.size(); }
        else if (s[1] == 'A' && h > 0) { buf = hist[--h]; pos = buf.size(); }
        else if (s[1] == 'B') { if (h + 1 < hist.size()) buf = hist[++h]; else { h = hist.size(); buf.clear(); } pos = buf.size(); }
        else if (s[1] == 'C' && pos < buf.size()) ++pos;
        else if (s[1] == 'D' && pos) --pos;
        else if (s[1] == 'H') pos = 0;
        else if (s[1] == 'F') pos = buf.size();
      }
      else if ((unsigned char)c >= 32) buf.insert(pos++, 1, c);
      draw();
    }
    tcsetattr(0, TCSAFLUSH, &orig);
    std::cout << "\n";
    if (!buf.empty() && (hist.empty() || hist.back() != buf)) hist.push_back(buf);
    out = buf;
    return ok;
  }
  static size_t count_esc(const std::string& s) { size_t n = 0; for (size_t i = 0; i < s.size(); ++i) if (s[i] == 27) { size_t j = s.find('m', i); n += j - i + 1; i = j; } else if ((s[i] & 0xC0) == 0x80) ++n; return n; }
};

// Is the input a complete chunk of C++? (balanced brackets outside strings and comments)
bool complete(const std::string& s) {
  int d = 0; char q = 0; bool lc = false, bc = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i], n = i + 1 < s.size() ? s[i + 1] : 0;
    if (lc) { if (c == '\n') lc = false; continue; }
    if (bc) { if (c == '*' && n == '/') { bc = false; ++i; } continue; }
    if (q) { if (c == '\\') ++i; else if (c == q) q = 0; continue; }
    if (c == '/' && n == '/') lc = true; else if (c == '/' && n == '*') bc = true;
    else if (c == '"' || (c == '\'' && !(i && std::isxdigit((unsigned char)s[i - 1])))) q = c;
    else if (c == '(' || c == '[' || c == '{') ++d; else if (c == ')' || c == ']' || c == '}') --d;
  }
  return d <= 0 && !bc && !q;
}

// ---------------------------------------------------------------- project view
struct Entry { std::string kind, text, uid, id; int depth = 0, line = 0, level = 0; };
std::vector<Entry> g_entries;   // the project flattened in display order
std::string g_root;

bool is_group(const std::string& k) { return k == "dir" || k == "file" || k == "sec" || k == "inc" || k == "others" || k == "fn"; }
void flatten(const J& n, int depth) {
  std::string k = n["k"].str();
  if (k == "entry") {
    flatten(n["main"], 0);
    if (!n["others"].a.empty()) {
      g_entries.push_back({"others", "not included from the entry point", "", "#others", 0});
      for (auto& f : n["others"].a) flatten(f, 1);
    }
  } else if (k == "inc") {
    std::string title = n["dup"].b ? n["id"].str() + " (already included above)" : n["file"]["title"].str();
    g_entries.push_back({"inc", title, n["uid"].str(), n["dup"].b ? "" : n["file"]["id"].str(), depth, (int)n["line"].num()});
    if (!n["dup"].b) for (auto& c : n["file"]["kids"].a) flatten(c, depth + 1);
  } else if (k == "fn") {
    g_entries.push_back({"fn", n["title"].str(), "", n["id"].str(), depth, (int)n["line"].num()});
    for (auto& c : n["kids"].a) flatten(c, depth + 1);
  } else if (k == "dir" || k == "file" || k == "sec") {
    if (!(k == "dir" && n["id"].str().empty())) g_entries.push_back({k, n["title"].str(), "", n["id"].str(), depth, 0, (int)n["level"].num()});
    for (auto& c : n["kids"].a) flatten(c, depth + (k == "dir" && n["id"].str().empty() ? 0 : 1));
  } else g_entries.push_back({k, n["text"].str(), n["uid"].str(), "", depth, (int)n["line"].num()});
}
void load_project() { g_entries.clear(); if (!g_root.empty()) flatten(J::parse(project_json(g_root)), 0); }
int cell_no(const std::string& uid) { int n = 0; for (auto& e : g_entries) if (e.kind == "code") { ++n; if (e.uid == uid) return n; } return 0; }
int group_no(const std::string& id) { int n = 0; for (auto& e : g_entries) if (is_group(e.kind)) { ++n; if (e.id == id) return n; } return 0; }

std::string md_line(std::string l) {   // light markdown: headings, **bold**, `code`, bullets
  if (l.rfind("#", 0) == 0) return std::string(BLD) + l.substr(l.find_first_not_of("# ")) + RST;
  std::string o; bool code = false, bold = false;
  for (size_t i = 0; i < l.size(); ++i) {
    if (l[i] == '`') { o += (code = !code) ? CYN : RST; continue; }
    if (l.compare(i, 2, "**") == 0) { o += (bold = !bold) ? BLD : RST; ++i; continue; }
    o += l[i];
  }
  if (l.rfind("- ", 0) == 0 || l.rfind("* ", 0) == 0) o = "• " + o.substr(2);
  return o + RST;
}
void print_entry(const Entry& e) {
  std::string ind(2 * e.depth, ' ');
  std::istringstream in(e.text); std::string l;
  if (e.kind == "dir") std::cout << ind << BLD << "▾ " << e.text << "/" << RST << DIM << "  g" << group_no(e.id) << RST << "\n";
  else if (e.kind == "file") std::cout << ind << BLD << CYN << "▾ " << e.text << RST << DIM << "  g" << group_no(e.id) << RST << "\n";
  else if (e.kind == "inc") std::cout << ind << CYN << "▾ #include " << RST << BLD << e.text << RST << DIM << "  g" << group_no(e.id) << RST << "\n";
  else if (e.kind == "fn") std::cout << ind << BLD << "▾ " << e.text << " { … }" << RST << DIM << "  g" << group_no(e.id) << RST << "\n";
  else if (e.kind == "others") std::cout << ind << DIM << "── " << e.text << " ──" << RST << "\n";
  else if (e.kind == "sec") std::cout << ind << BLD << std::string(e.level, '#') << " " << e.text << RST << DIM << "  g" << group_no(e.id) << RST << "\n";
  else if (e.kind == "md") while (std::getline(in, l)) std::cout << ind << md_line(l) << "\n";
  else if (e.kind == "ccode") while (std::getline(in, l)) std::cout << ind << DIM << "// " << l << RST << "\n";
  else {
    bool first = true;
    while (std::getline(in, l)) {
      std::string tag = first ? "[" + std::to_string(cell_no(e.uid)) + "]" : "";
      std::cout << ind << GRN << std::setw(5) << std::left << tag << std::right << RST << l << "\n";
      first = false;
    }
  }
}
// Print the tree; groups deeper than `depth` are folded into a one-line summary.
void print_tree(int depth, const std::string& only = "") {
  bool in = only.empty(); int base = 0, fold = -1;
  for (size_t i = 0; i < g_entries.size(); ++i) {
    auto& e = g_entries[i];
    bool group = is_group(e.kind);
    if (!only.empty()) {
      if (group && e.id == only) { in = true; base = e.depth; print_entry(e); continue; }
      if (in && e.depth <= base && i && group) break;
      if (!in) continue;
    }
    if (fold >= 0 && e.depth > fold) continue;
    fold = -1;
    if (group && e.depth - base >= depth && only.empty()) {
      int cells = 0;
      for (size_t j = i + 1; j < g_entries.size() && g_entries[j].depth > e.depth; ++j) cells += g_entries[j].kind == "code";
      std::string ind(2 * e.depth, ' ');
      std::cout << ind << DIM << "▸ " << RST << (e.kind == "sec" ? std::string(e.level, '#') + " " : "") << e.text << DIM << "  (" << cells << " cells)  g" << group_no(e.id) << RST << "\n";
      fold = e.depth;
      continue;
    }
    print_entry(e);
  }
}

// Prints everything a request produces. With `show_cells`, a cell's source (and the markdown before it) prints when it starts running.
J handle(Kernel& k, const std::string& op, const std::string& fields, std::vector<Entry>* show_cells = nullptr) {
  size_t shown = 0;
  auto show_until = [&](const std::string& uid) {
    if (!show_cells || std::none_of(show_cells->begin(), show_cells->end(), [&](const Entry& e) { return e.uid == uid; })) return;
    while (shown < show_cells->size()) { auto& e = (*show_cells)[shown++]; print_entry(e); if (e.uid == uid) break; }
  };
  J r = k.call(op, fields, [&](const std::string& s, const std::string& d) {
    if (s == "out") { std::cout << d << std::flush; return; }
    if (s == "err") { std::cout << RED << d << RST << std::flush; return; }
    J e = J::parse(d); std::string ev = e["ev"].str();
    if (ev == "cell") show_until(e["uid"].str());
    else if (ev == "compile" && !e["diag"].str().empty()) { if (!e["ok"].b) show_until(""); std::cout << e["diag"].str(); }
    else if (ev == "status") std::cout << DIM << e["msg"].str() << RST << "\n";
    else if (ev == "display" || ev == "node") render(std::cout, e["node"], "", "");
    else if (ev == "result") std::cout << DIM << "     ⤷ " << e["t"].str() << RST << mkref(e["ref"].str(), "reveal") << "\n";
    else if (ev == "rich") {
      std::string m = e["mime"].str();
      if (m == "text/x-ansi" || m.rfind("text/", 0) == 0) std::cout << (m == "text/x-stack" ? std::string(CYN) + "── stack snapshot (gdb) ──" + RST + "\n" : "") << e["data"].str() << "\n";
      else std::cout << DIM << "[" << m << " output — view it in the web UI]" << RST << "\n";
    }
    else if (ev == "error") std::cout << RED << BLD << e["type"].str() << RST << RED << ": " << e["what"].str() << RST << "\n";
    else if (ev == "crash") std::cout << RED << BLD << e["sig"].str() << " (fault address " << e["addr"].str() << ")" << RST << "\n" << e["bt"].str();
    else if (ev == "stats") {
      std::cout << DIM << "⏱ compile " << (long)e["compile_ms"].num() << " ms · run " << std::setprecision(3) << e["run_ms"].num() << " ms";
      if (e["instructions"].num() > 0) std::cout << " · " << si(e["instructions"].num()) << " instr · IPC " << std::setprecision(2) << e["instructions"].num() / std::max(1.0, e["cycles"].num());
      if (e["allocs"].num() > 0) std::cout << " · " << (long)e["allocs"].num() << " allocs " << human(e["alloc_bytes"].num()) << " (net " << human(e["heap_net"].num()) << ")";
      std::cout << " · stack " << human(e["stack"].num()) << RST << std::setprecision(6) << "\n";
    }
    else if (ev == "restored") std::cout << YEL << "⟲ kernel state was lost (" << e["why"].str() << "); restored to after batch " << (long)e["last"].num() << RST << "\n";
    else if (ev == "asm") {
      auto& a = e["asm"];
      for (auto& f : a["fns"].a) {
        std::cout << BLD << f["fn"].str() << ":" << RST << "\n";
        std::string prev;
        for (auto& i : f["ins"].a) {
          int fi = (int)i[0].num(), l = (int)i[1].num();
          std::string loc = fi >= 0 ? a["files"][fi].str().substr(a["files"][fi].str().rfind('/') + 1) + ":" + std::to_string(l) : "";
          std::cout << (fi >= 0 ? (loc != prev ? std::string(CYN) + loc : std::string("")) : std::string(DIM) + "·");
          std::cout << "\t" << i[2].str() << RST << "\n"; prev = loc;
        }
      }
    }
    else if (ev == "vars") for (auto& v : e["vars"].a) render(std::cout, v["node"], "", v["name"].str());
    else if (ev == "heap") print_heap(e["heap"], false);
    else if (ev == "leaks") print_heap(e["leaks"], true);
    else if (ev == "macro") std::cout << YEL << e["m"]["text"].str() << RST << "\n";
    else if (ev == "layout" || ev == "source") std::cout << e["text"].str() << "\n";
    else if (ev == "stopped") std::cout << YEL << "■ stopped" << (e["skipped"].a.empty() ? "" : "; " + std::to_string(e["skipped"].a.size()) + " later cell(s) skipped") << RST << "\n";
    else if (ev == "done" && e.has("why")) std::cout << YEL << "⟲ " << e["why"].str() << (e["why"].str() == "nothing to undo" ? "" : " — this run was discarded") << RST << "\n";
  });
  if (show_cells && r["ok"].b) while (shown < show_cells->size()) print_entry((*show_cells)[shown++]);
  return r;
}

int g_last_batch = 0;
std::string cells_json(const std::vector<Src>& cells) {
  std::string o;
  for (auto& c : cells) o += (o.empty() ? "" : ",") + std::string("{\"uid\":") + jq(c.uid) + ",\"code\":" + jq(c.code) + ",\"file\":" + jq(c.file) + ",\"line\":" + std::to_string(c.line) + "}";
  return "\"cells\":[" + o + "]";
}
bool run_batch(Kernel& k, const std::vector<Src>& cells, std::vector<Entry>* show = nullptr) {
  if (cells.empty()) { if (show) for (auto& e : *show) print_entry(e); return true; }
  g_last_batch = k.next_id++;
  return handle(k, "exec", "\"id\":" + std::to_string(g_last_batch) + "," + cells_json(cells), show)["ok"].b;
}
bool exec(Kernel& k, const std::string& code) {
  int n = k.next_id;
  bool ok = run_batch(k, {{"in" + std::to_string(n), code, "", 1}});
  if (ok) g_history.push_back(code);
  return ok;
}
// Entries of a group, for printing alongside its run.
std::vector<Entry> group_entries(const std::string& id) {
  std::vector<Entry> v; bool in = id.empty(); int base = -1;
  for (auto& e : g_entries) {
    bool group = is_group(e.kind);
    if (!in && group && e.id == id) { in = true; base = e.depth; v.push_back(e); continue; }
    if (in && base >= 0 && group && e.depth <= base) break;
    if (in) v.push_back(e);
  }
  return v;
}
// Resolve "5" (cell), "g3" (group) or a path-like id to a group id or cell uid.
bool resolve_target(const std::string& a, std::string& uid, std::string& group) {
  uid.clear(); group.clear();
  if (a.empty() || a == "all") return true;
  int n = std::atoi(a.c_str() + (a[0] == 'g'));
  if (n > 0) {
    int i = 0;
    for (auto& e : g_entries) {
      bool g = is_group(e.kind);
      if (a[0] == 'g' ? g : e.kind == "code") if (++i == n) { (a[0] == 'g' ? group : uid) = a[0] == 'g' ? e.id : e.uid; return true; }
    }
    return false;
  }
  group = a; return true;
}
std::string edit_text(const std::string& text, const std::string& name) {
  std::string tmp = "/tmp/zeno-edit-" + std::to_string(getpid()) + "-" + name;
  write_file(tmp, text);
  const char* ed = getenv("VISUAL") ? getenv("VISUAL") : getenv("EDITOR") ? getenv("EDITOR") : "vim";
  pid_t p = fork();
  if (p == 0) { signal(SIGINT, SIG_DFL); execlp(ed, ed, tmp.c_str(), (char*)nullptr); _exit(127); }
  waitpid(p, nullptr, 0);
  std::string out = read_file(tmp);
  unlink(tmp.c_str());
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

void help() {
  std::cout << BLD << "Zeno" << RST << " — C++ notebook. Type C++ statements; values are kept but hidden: reveal with .x N.\n"
            << "  .tree [depth]     project outline (groups fold below depth; default 2)\n"
            << "  .show G|N         print a group (g3) or cell (5) in full\n"
            << "  .run [N|gN|all]   run a cell, a group, or everything    .edit [N]   edit a cell in $EDITOR (no N: last input)\n"
            << "  .x N              reveal / expand reference #N           .vars       variables in scope\n"
            << "  .asm              disassembly of the last run             .layout T   memory layout of type T (gdb)\n"
            << "  .heap / .leaks    live heap by site & cell / unreachable  .macro N NAME   expand a macro use in cell N\n"
            << "  .undo             roll back the last run                  .restart    fresh kernel\n"
            << "  .save F           append this session's inputs to F       .src        generated translation unit\n"
            << "  magics (as // comments in files): //%flags -O3  //%cflags ..  //%link -lfoo  //%check  //%asm\n"
            << "  in code: TIMEIT(expr)  zeno::plot(v), zeno::hist(v), zeno::timeit(f), zeno::display(x), zeno::here(), zeno::read_csv(path)\n"
            << "  Ctrl-C stops a run at a clean point (zeno::interrupted() lets a cell wrap up; later cells are skipped).\n"
            << "  Ctrl-\\ kills it, discarding the run.\n";
}
}  // namespace

int run_main(const std::string& path) {
  Kernel k; k.mode = "tui"; k.start();
  g_root = path; load_project();
  int fails = 0;
  if (!g_entries.empty() && g_entries[0].kind == "file" && g_entries[0].depth == 0 && J::parse(project_json(g_root))["k"].str() == "entry") {
    std::vector<Entry> show;   // the entry point (with its includes) is one run
    for (auto& e : g_entries) { if (e.kind == "others") break; show.push_back(e); }
    fails += !run_batch(k, cells_under(g_root, ""), &show);
  } else
    for (auto& e : g_entries) {   // browsing a folder: one run per file, so a failure only rolls back that file
      if (e.kind != "file") continue;
      auto show = group_entries(e.id);
      if (!run_batch(k, cells_under(g_root, e.id), &show)) ++fails;
      std::cout << "\n";
    }
  k.stop();
  if (fails) std::cout << RED << fails << " run(s) failed" << RST << "\n";
  return fails ? 1 : 0;
}

int tui_main(const std::string& path) {
  Kernel k; k.mode = "tui"; k.start(); g_kernel = &k;
  signal(SIGINT, [](int) { if (g_kernel) g_kernel->interrupt(false); });   // Ctrl-C: stop at a clean point
  signal(SIGQUIT, [](int) { if (g_kernel) g_kernel->interrupt(true); });   // Ctrl-\: kill, discarding the run
  std::cout << BLD << "Zeno" << RST << DIM << " — C++ notebook. .help for commands, Ctrl-D to quit." << RST << "\n";
  g_root = path; load_project();
  if (!g_root.empty()) print_tree(2);
  Editor ed;
  for (;;) {
    std::string cell, line;
    std::string p = std::string(GRN) + "In [" + std::to_string(k.next_id) + "]: " + RST;
    if (!ed.read(p, line)) break;
    if (line == "\x03" || line.empty()) continue;
    if (line[0] == '.') {
      std::istringstream in(line.substr(1)); std::string cmd, arg; in >> cmd; std::getline(in >> std::ws, arg);
      std::string uid, group;
      if (cmd == "q" || cmd == "quit" || cmd == "exit") break;
      else if (cmd == "help" || cmd == "h") help();
      else if (cmd == "tree") { load_project(); print_tree(arg.empty() ? 2 : std::atoi(arg.c_str())); }
      else if (cmd == "show") {
        load_project();
        if (!resolve_target(arg, uid, group)) std::cout << "no such cell/group\n";
        else if (!uid.empty()) { for (auto& e : g_entries) if (e.uid == uid) print_entry(e); }
        else print_tree(100, group);
      }
      else if (cmd == "run") {
        load_project();
        if (!resolve_target(arg, uid, group)) { std::cout << "no such cell/group\n"; continue; }
        if (!uid.empty()) { Src s; if (find_cell(g_root, uid, s)) { std::vector<Entry> show; for (auto& e : g_entries) if (e.uid == uid) show.push_back(e); run_batch(k, {s}, &show); } }
        else { auto show = group_entries(group); run_batch(k, cells_under(g_root, group), &show); }
      }
      else if (cmd == "edit") {
        load_project();
        if (arg.empty()) {   // re-edit the last REPL input and run it
          std::string t = edit_text(g_history.empty() ? "" : g_history.back(), "input.cpp");
          if (!t.empty()) exec(k, t);
        } else if (resolve_target(arg, uid, group) && !uid.empty()) {
          Src s;
          if (!find_cell(g_root, uid, s)) { std::cout << "no such cell\n"; continue; }
          std::string t = edit_text(s.code, "cell.cpp");
          if (t == s.code) { std::cout << DIM << "unchanged" << RST << "\n"; continue; }
          replace_cell(g_root, uid, t);
          load_project();
          std::cout << DIM << "saved to " << s.file << " — .run " << arg << " to execute" << RST << "\n";
          for (auto& e : g_entries) if (e.uid == uid) print_entry(e);
        } else std::cout << "usage: .edit N\n";
      }
      else if (cmd == "macro") {
        std::istringstream a(arg); std::string n, name; a >> n >> name;
        Src s;
        if (!resolve_target(n, uid, group) || uid.empty() || !find_cell(g_root, uid, s)) { std::cout << "usage: .macro N NAME\n"; continue; }
        size_t at = s.code.find(name);
        if (at == std::string::npos) { std::cout << name << " not used in cell " << n << "\n"; continue; }
        handle(k, "macro", "\"code\":" + jq(s.code) + ",\"at\":" + std::to_string(at));
      }
      else if (cmd == "vars") handle(k, "vars", "");
      else if (cmd == "heap") handle(k, "heap", "");
      else if (cmd == "leaks") handle(k, "leaks", "");
      else if (cmd == "undo") { J r = handle(k, "undo", ""); if (r["ok"].b) { if (!g_history.empty()) g_history.pop_back(); std::cout << YEL << "⟲ state is now as after batch " << (long)r["last"].num() << RST << "\n"; } }
      else if (cmd == "asm") handle(k, "asm", "\"id\":" + (arg.empty() ? std::to_string(g_last_batch) : arg));
      else if (cmd == "src") handle(k, "source", "\"id\":" + (arg.empty() ? std::to_string(g_last_batch) : arg));
      else if (cmd == "layout") handle(k, "layout", "\"type\":" + jq(arg));
      else if (cmd == "x") {
        size_t n = std::atoi(arg.c_str());
        if (n >= 1 && n <= g_refs.size()) handle(k, "expand", "\"ref\":" + jq(g_refs[n - 1])); else std::cout << "no reference #" << arg << "\n";
      }
      else if (cmd == "restart") { k.stop(); g_history.clear(); k.start(); std::cout << DIM << "kernel restarted" << RST << "\n"; }
      else if (cmd == "save") {
        std::string o = read_file(arg);
        for (auto& h : g_history) o += (o.empty() || o.back() == '\n' ? "" : "\n") + std::string(o.empty() ? "" : "\n") + h + "\n";
        write_file(arg, o); std::cout << DIM << "appended " << g_history.size() << " inputs to " << arg << RST << "\n";
      }
      else std::cout << "unknown command ." << cmd << " (.help)\n";
      continue;
    }
    cell = line;
    while (!complete(cell)) {
      std::string more;
      if (!ed.read(std::string(DIM) + "   ...: " + RST, more) || more == "\x03") { cell.clear(); break; }
      if (more.empty()) break;
      cell += "\n" + more;
    }
    if (!cell.empty()) exec(k, cell);
  }
  k.stop();
  return 0;
}
