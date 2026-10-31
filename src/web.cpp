// Browser frontend: a small HTTP server (one thread per connection) that serves the page,
// relays kernel events as NDJSON streams, edits cells in source files, and runs vim in ptys.
#include "common.hpp"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <csignal>
#include <fcntl.h>
#include <cstring>
#include <filesystem>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <regex>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <pty.h>
#include <random>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern const char* const kUiHtml;

namespace {
std::string g_root, g_token, g_server_file;
int g_port = 0;
std::atomic<long> g_version{0};   // bumped whenever a source file changes on disk
std::mutex g_vmu;
std::condition_variable g_vcv;
Kernel g_k;
std::mutex g_kmu;   // one kernel request at a time (interrupt bypasses it)

struct Req { std::string method, path, body; std::map<std::string, std::string> q, hdr; };

std::string urldecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) { o += char(std::stoi(s.substr(i + 1, 2), nullptr, 16)); i += 2; }
    else o += s[i] == '+' ? ' ' : s[i];
  }
  return o;
}
bool read_req(int fd, Req& r) {
  std::string buf; char tmp[65536];
  size_t he;
  while ((he = buf.find("\r\n\r\n")) == std::string::npos) {
    ssize_t k = read(fd, tmp, sizeof tmp); if (k <= 0) return false; buf.append(tmp, k);
    if (buf.size() > (1 << 20)) return false;
  }
  std::istringstream in(buf.substr(0, he));
  std::string target, line; in >> r.method >> target; std::getline(in, line);
  while (std::getline(in, line)) {
    size_t c = line.find(':'); if (c == std::string::npos) continue;
    std::string k = line.substr(0, c), v = line.substr(c + 1);
    for (auto& ch : k) ch = std::tolower(ch);
    v.erase(0, v.find_first_not_of(' ')); if (!v.empty() && v.back() == '\r') v.pop_back();
    r.hdr[k] = v;
  }
  size_t qm = target.find('?');
  r.path = target.substr(0, qm);
  if (qm != std::string::npos) {
    std::istringstream qs(target.substr(qm + 1));
    for (std::string kv; std::getline(qs, kv, '&');) { size_t e = kv.find('='); r.q[urldecode(kv.substr(0, e))] = e == std::string::npos ? "" : urldecode(kv.substr(e + 1)); }
  }
  size_t len = r.hdr.count("content-length") ? std::stoul(r.hdr["content-length"]) : 0;
  r.body = buf.substr(he + 4);
  while (r.body.size() < len) { ssize_t k = read(fd, tmp, sizeof tmp); if (k <= 0) return false; r.body.append(tmp, k); }
  return true;
}
bool send_all(int fd, std::string_view s) {
  for (size_t w = 0; w < s.size();) { ssize_t k = send(fd, s.data() + w, s.size() - w, MSG_NOSIGNAL); if (k <= 0) return false; w += k; }
  return true;
}
void respond(int fd, int code, const std::string& type, const std::string& body) {
  send_all(fd, "HTTP/1.1 " + std::to_string(code) + (code == 200 ? " OK" : " Error") + "\r\nContent-Type: " + type +
               "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body);
}
struct Stream {   // chunked response
  int fd; bool ok = true;
  Stream(int f, const std::string& type) : fd(f) { ok = send_all(fd, "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nTransfer-Encoding: chunked\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"); }
  void chunk(std::string_view s) { if (!ok || s.empty()) return; char h[32]; std::snprintf(h, 32, "%zx\r\n", s.size()); ok = send_all(fd, h) && send_all(fd, s) && send_all(fd, "\r\n"); }
  ~Stream() { if (ok) send_all(fd, "0\r\n\r\n"); }
};

// Forward a kernel request, streaming every event as one JSON line.
void kernel_stream(int fd, const std::string& op, const std::string& fields, const std::string& first = "") {
  Stream s(fd, "application/x-ndjson");
  std::lock_guard<std::mutex> l(g_kmu);
  if (!first.empty()) s.chunk(first + "\n");
  if (g_k.pid < 0) { g_k.start(); s.chunk("{\"ev\":\"status\",\"msg\":\"kernel restarted\"}\n"); }
  g_k.call(op, fields, [&](const std::string& kind, const std::string& d) {
    if (kind == "ev") s.chunk(d + "\n");
    else s.chunk("{\"ev\":" + jq(kind) + ",\"s\":" + jq(d) + "}\n");
  });
}
std::string fields_of(const J& j, std::initializer_list<const char*> skip) {   // re-serialize all keys but `skip`
  std::string o;
  for (auto& [k, v] : j.o) {
    bool sk = false; for (auto s : skip) sk |= k == s;
    if (!sk) o += (o.empty() ? "" : ",") + jq(k) + ":" + v.dump();
  }
  return o;
}
std::string cells_json(const std::vector<Src>& cells) {
  std::string o;
  for (auto& c : cells) o += (o.empty() ? "" : ",") + std::string("{\"uid\":") + jq(c.uid) + ",\"code\":" + jq(c.code) + ",\"file\":" + jq(c.file) + ",\"line\":" + std::to_string(c.line) + "}";
  return "[" + o + "]";
}

// ---------------------------------------------------------------- outputs: kept per cell, broadcast to every page
std::mutex g_rmu;
// Keyed by file + source text rather than uid, so results stay with their cell when others are inserted or removed.
std::map<std::string, std::vector<std::string>> g_results;   // cell key -> events of its last run
std::map<std::string, int> g_batch_of;                        // cell key -> batch id (for disassembly)
std::string ckey(const Src& c) { return c.file + '\x1f' + c.code; }
std::string key_of(const std::string& uid) { Src c; return find_cell(g_root, uid, c) ? ckey(c) : uid; }
std::deque<std::pair<long, std::string>> g_log;               // recent events, for /api/events subscribers
long g_seq = 0;
// g_hist mirrors the kernel's snapshot chain: one entry per successful run, holding the cells it executed.
struct Batch { int id; std::vector<std::string> keys; };
std::vector<Batch> g_hist;
bool g_hist_unknown = false;   // the kernel lost state behind our back: next sync restarts

std::condition_variable g_lcv;

void publish(const std::string& line) {
  { std::lock_guard<std::mutex> l(g_rmu); g_log.push_back({++g_seq, line}); if (g_log.size() > 20000) g_log.pop_front(); }
  g_lcv.notify_all();
}
std::string strip_ansi(const std::string& s) { static const std::regex re("\x1b\\[[0-9;]*[mK]"); return std::regex_replace(s, re, ""); }

// Plain-text account of a cell's last run, for the assistant and for agents.
std::string results_text(const std::string& uid) {
  std::string key = key_of(uid);
  std::lock_guard<std::mutex> l(g_rmu);
  auto it = g_results.find(key);
  if (it == g_results.end()) return "(not run since it was last edited)";
  std::string o;
  for (auto& line : it->second) {
    J e = J::parse(line); std::string ev = e["ev"].str();
    if (ev == "out" || ev == "err") o += (ev == "err" ? "[stderr] " : "") + e["s"].str();
    else if (ev == "compile" && !e["diag"].str().empty()) o += "[compiler]\n" + strip_ansi(e["diag"].str()) + "\n";
    else if (ev == "result") o += "[value] " + e["t"].str() + "  (reveal ref " + e["ref"].str() + ")\n";
    else if (ev == "error") o += "[" + e["type"].str() + "] " + e["what"].str() + "\n";
    else if (ev == "crash") o += "[crash " + e["sig"].str() + "]\n" + e["bt"].str() + "\n";
    else if (ev == "rich") o += "[" + e["mime"].str() + " output]\n";
    else if (ev == "done" && e.has("why")) o += "[run rolled back: " + e["why"].str() + "]\n";
    else if (ev == "stats") o += "[stats] compile " + std::to_string((int)e["compile_ms"].num()) + " ms, run " + std::to_string(e["run_ms"].num()) + " ms\n";
  }
  return o.empty() ? "(ran; no output)" : o;
}

// Run cells: stream events to the requester, remember them per cell, and broadcast them.
void run_cells(int fd, const std::vector<Src>& cells, const std::string& origin) {
  int id;
  { std::lock_guard<std::mutex> l(g_kmu); id = g_k.next_id++; }
  std::string uids;
  for (auto& c : cells) uids += (uids.empty() ? "" : ",") + jq(c.uid);
  {
    std::lock_guard<std::mutex> l(g_rmu);
    for (auto& c : cells) { g_results[ckey(c)].clear(); g_batch_of[ckey(c)] = id; }
  }
  std::map<std::string, std::string> keys;
  for (auto& c : cells) keys[c.uid] = ckey(c);
  std::string cur = cells.empty() ? "" : keys[cells[0].uid], tag = ",\"origin\":" + jq(origin) + "}";
  Stream s(fd, "application/x-ndjson");
  auto emit = [&](const std::string& line) {
    s.chunk(line + "\n");
    publish(line.substr(0, line.size() - 1) + tag);
    J e = J::parse(line);
    std::string ev = e["ev"].str();
    if (ev == "cell") cur = keys[e["uid"].str()];
    std::lock_guard<std::mutex> l(g_rmu);
    if (ev == "result") g_results[keys[e["uid"].str()]].push_back(line);
    else if (ev != "cell" && ev != "start" && ev != "batch" && !cur.empty()) g_results[cur].push_back(line);
  };
  emit("{\"ev\":\"batch\",\"id\":" + std::to_string(id) + ",\"uids\":[" + uids + "]}");
  std::lock_guard<std::mutex> l(g_kmu);
  if (g_k.pid < 0) g_k.start();
  J done = g_k.call("exec", "\"id\":" + std::to_string(id) + ",\"cells\":" + cells_json(cells), [&](const std::string& kind, const std::string& d) {
    if (kind == "ev") { if (d.find("\"ev\":\"restored\"") != std::string::npos) g_hist_unknown = true; emit(d); }
    else emit("{\"ev\":" + jq(kind) + ",\"s\":" + jq(d) + "}");
  });
  if (done["ok"].b) { Batch b{id, {}}; for (auto& c : cells) b.keys.push_back(ckey(c)); g_hist.push_back(b); }
  if (g_k.pid < 0) { g_hist.clear(); g_hist_unknown = false; }
}

// ---------------------------------------------------------------- sync: keep the state equal to a top-to-bottom run

struct Plan { int undo = 0; bool restart = false; std::vector<Src> run; std::vector<std::string> dirty; };

// Which cells must (re)run so that the state equals running every cell up to `upto` (or all) in file order,
// and how many runs must be rolled back first. Uses name-level dependencies, see deps_of().
Plan make_plan(const std::string& upto) {
  Plan p;
  auto all = cells_under(g_root, "");
  std::vector<Src> N;
  for (auto& c : all) { N.push_back(c); if (!upto.empty() && c.uid == upto) break; }
  static std::map<std::string, Deps> cache;
  auto deps = [&](const std::string& key) -> Deps& { auto it = cache.find(key); if (it == cache.end()) it = cache.emplace(key, deps_of(key.substr(key.find('\x1f') + 1))).first; return it->second; };
  std::set<std::string> live; for (auto& c : all) live.insert(ckey(c));
  // reads of a cell include what the functions it calls read (transitively)
  std::map<std::string, std::set<std::string>> fn_uses;
  for (auto& c : all) { auto& d = deps(ckey(c)); if (d.decl_only) for (auto& n : d.defs) fn_uses[n].insert(d.uses.begin(), d.uses.end()); }
  auto reads = [&](const std::string& key) {
    std::set<std::string> r = deps(key).uses, seen;
    std::vector<std::string> work(r.begin(), r.end());
    while (!work.empty()) { std::string n = work.back(); work.pop_back(); if (!seen.insert(n).second) continue; for (auto& u : fn_uses[n]) if (r.insert(u).second) work.push_back(u); }
    return r;
  };
  auto writes = [&](const std::string& key) { auto& d = deps(key); std::set<std::string> w = d.defs; if (!d.decl_only) w.insert(d.uses.begin(), d.uses.end()); return w; };
  auto meets = [](const std::set<std::string>& a, const std::set<std::string>& b) { for (auto& x : a) if (b.count(x)) return true; return false; };
  if (g_hist_unknown || g_k.chain.size() < g_hist.size() + 1) { p.restart = true; p.run = N; for (auto& c : N) p.dirty.push_back(c.uid); return p; }
  size_t keep = g_hist.size();
  for (;;) {
    std::map<std::string, size_t> ran;   // key -> batch index
    for (size_t j = 0; j < keep; ++j) for (auto& k : g_hist[j].keys) ran[k] = j;
    size_t rb = keep;
    std::vector<bool> dirty(N.size());
    std::set<std::string> dirty_writes, redefined;
    for (size_t i = 0; i < N.size(); ++i) {
      std::string k = ckey(N[i]);
      dirty[i] = !ran.count(k) || meets(reads(k), dirty_writes);
      if (dirty[i]) { auto w = writes(k); dirty_writes.insert(w.begin(), w.end()); redefined.insert(deps(k).defs.begin(), deps(k).defs.end()); }
    }
    // A cell edited or deleted since it ran left its effects behind. That's harmless only if everything it
    // wrote gets declared afresh by the re-run (the new declaration shadows the old); otherwise roll back.
    for (size_t j = 0; j < keep; ++j) for (auto& k : g_hist[j].keys)
      if (!live.count(k)) for (auto& w : writes(k)) if (!redefined.count(w)) { rb = std::min(rb, j); break; }
    // Re-running a dirty cell now is only right if nothing after it (left alone) changed what it reads.
    std::set<std::string> later_writes;
    for (size_t i = N.size(); i-- > 0;) {
      std::string k = ckey(N[i]);
      if (dirty[i] && meets(reads(k), later_writes))
        for (size_t j = i + 1; j < N.size(); ++j) if (!dirty[j] && meets(writes(ckey(N[j])), reads(k))) rb = std::min(rb, ran[ckey(N[j])]);
      if (!dirty[i] && ran.count(k)) { auto w = writes(k); later_writes.insert(w.begin(), w.end()); }
    }
    // Cells run out of file order are only harmless if they don't touch the same names.
    long prev = -1; std::string prev_key;
    std::map<std::string, size_t> pos; for (size_t i = 0; i < all.size(); ++i) pos[ckey(all[i])] = i;
    for (size_t j = 0; j < keep; ++j) for (auto& k : g_hist[j].keys) {
      if (!pos.count(k)) continue;
      long at = (long)pos[k];
      if (at < prev && meets(writes(k), reads(prev_key))) rb = std::min(rb, j);
      if (at > prev) { prev = at; prev_key = k; }
    }
    if (rb >= keep) {
      p.undo = (int)(g_hist.size() - keep);
      for (size_t i = 0; i < N.size(); ++i) if (dirty[i]) { p.run.push_back(N[i]); p.dirty.push_back(N[i].uid); }
      return p;
    }
    keep = rb;
  }
}

// ---------------------------------------------------------------- assistant
std::string chat_system(const std::string& focus) {
  std::string sys =
      "You are the assistant inside zeno, a C++ notebook. A notebook is a folder of ordinary C++ files; the entry file's local "
      "#includes form the hierarchy. Every top-level statement is a cell (declarations, function definitions, expression "
      "statements, if/for blocks). Comments on their own line are markdown. Cells run top to bottom as static initializers in a "
      "persistent process; redeclaring a name in a later cell shadows the earlier one. Values of expression cells are kept but "
      "hidden. Useful helpers: zeno::plot(v), zeno::hist(v, bins), zeno::scatter(x, y), zeno::timeit(f), zeno::display(x), "
      "zeno::read_csv(path), zeno::here() (gdb stack snapshot). Magic comment lines: //%timeit EXPR, //%flags -O3, //%link -lfoo or file.cpp.\n\n"
      "To propose a change to an existing cell, reply with a fenced block whose info string is `cpp replace=<uid>`; to add a new "
      "cell after one, use `cpp after=<uid>` (the uid without brackets, e.g. `cpp replace=main.cpp:4`). The user applies them with one click. Keep cells small: one statement each. Be concise.\n\n"
      "Notebook outline (uids in brackets):\n" + outline_text(g_root, 10000);
  Src c;
  if (!focus.empty() && find_cell(g_root, focus, c))
    sys += "\nThe user is looking at cell [" + focus + "]:\n```cpp\n" + c.code + "\n```\nIts last run:\n" + results_text(focus) + "\n";
  return sys;
}

// Vim script that asks the server for completions (insert mode: Ctrl-Space or Ctrl-X Ctrl-U).
std::string vim_completion_script(const std::string& uid) {
  std::string url = "http://127.0.0.1:" + std::to_string(g_port);
  return "let g:zeno_url = '" + url + "'\nlet g:zeno_token = '" + g_token + "'\nlet g:zeno_uid = '" + uid + "'\n" + R"vim(
function! ZenoComplete(findstart, base) abort
  if a:findstart
    return col('.') - 1 - len(matchstr(getline('.')[: col('.') - 2], '\k*$'))
  endif
  let l:line = getline('.')
  let l:col = col('.') - 1
  let l:pre = join(getline(1, line('.') - 1) + [l:line[: l:col - 1]], "\n") . a:base
  let l:suf = l:line[l:col :] . "\n" . join(getline(line('.') + 1, '$'), "\n")
  let l:body = json_encode({'uid': g:zeno_uid, 'prefix': l:pre, 'suffix': l:suf})
  let l:out = system('curl -s -m 30 -X POST -H "X-Token: ' . g:zeno_token . '" --data-binary @- ' . g:zeno_url . '/api/complete', l:body)
  try
    let l:items = json_decode(l:out).items
  catch
    return []
  endtry
  return map(filter(l:items, 'v:val !~ "\n"'), '{"word": a:base . v:val, "abbr": a:base . v:val, "menu": "[zeno]"}')
endfunction
set completefunc=ZenoComplete
set completeopt=menuone,noinsert
inoremap <C-Space> <C-x><C-u>
inoremap <Nul> <C-x><C-u>
)vim";
}

// ---------------------------------------------------------------- vim in a pty
struct Term { int master = -1; pid_t pid = -1; std::string file, uid, orig; };
std::map<int, std::shared_ptr<Term>> g_terms;
std::mutex g_tmu;
int g_next_term = 1;

int open_term(const std::string& uid, int rows, int cols) {
  Src s;
  if (!find_cell(g_root, uid, s) || is_readonly(g_root, s.file)) return -1;
  auto t = std::make_shared<Term>();
  t->uid = uid; t->orig = s.code;
  t->file = "/tmp/zeno-vim-" + std::to_string(getpid()) + "-" + std::to_string(g_next_term) + ".cpp";
  write_file(t->file, s.code + "\n");
  std::string vimrc = t->file + ".vim";
  write_file(vimrc, vim_completion_script(uid));
  winsize ws{(unsigned short)std::max(rows, 5), (unsigned short)std::max(cols, 20), 0, 0};   // vim crashes in tiny terminals
  pid_t pid = forkpty(&t->master, nullptr, nullptr, &ws);
  if (pid < 0) return -1;
  if (pid == 0) {
    setenv("TERM", "xterm-256color", 1);
    // Report the buffer's line count with an OSC sequence so the page can size the terminal to fit.
    const char* report = "call echoraw(printf(\"\\e]777;lines=%d\\x07\", line('$')))";
    std::string au = std::string("autocmd TextChanged,TextChangedI,VimResized,BufWinEnter * ") + report;
    const char* ed = getenv("ZENO_VIM") ? getenv("ZENO_VIM") : "vim";
    execlp(ed, ed, "-S", vimrc.c_str(), "-c", "set laststatus=0 noruler shortmess+=FI fillchars+=eob:\\ ", "-c", au.c_str(), "-c", report, t->file.c_str(), (char*)nullptr);
    _exit(127);
  }
  t->pid = pid;
  std::lock_guard<std::mutex> l(g_tmu);
  g_terms[g_next_term] = t;
  return g_next_term++;
}
std::shared_ptr<Term> term(const std::string& id) {
  std::lock_guard<std::mutex> l(g_tmu);
  auto it = g_terms.find(std::atoi(id.c_str()));
  return it == g_terms.end() ? nullptr : it->second;
}
void term_stream(int fd, std::shared_ptr<Term> t, int id) {
  {
    Stream s(fd, "application/octet-stream");
    char buf[16384];
    for (;;) {
      ssize_t k = read(t->master, buf, sizeof buf);
      if (k <= 0) break;
      s.chunk(std::string_view(buf, k));
      if (!s.ok) { kill(t->pid, SIGTERM); break; }
    }
  }
  waitpid(t->pid, nullptr, 0);
  close(t->master);
  std::string text = read_file(t->file);
  while (!text.empty() && text.back() == '\n') text.pop_back();
  if (text != t->orig) replace_cell(g_root, t->uid, text);
  unlink(t->file.c_str());
  unlink((t->file + ".vim").c_str());
  std::lock_guard<std::mutex> l(g_tmu);
  g_terms.erase(id);
}

// ---------------------------------------------------------------- requests
void handle(int fd) {
  Req r;
  if (!read_req(fd, r)) { close(fd); return; }
  if (r.path == "/" || r.path == "/index.html") { respond(fd, 200, "text/html; charset=utf-8", kUiHtml); close(fd); return; }
  std::string tok = r.hdr.count("x-token") ? r.hdr["x-token"] : r.q["token"];
  if (!g_token.empty() && tok != g_token) { respond(fd, 403, "text/plain", "bad token: open the URL printed by zeno"); close(fd); return; }
  J b = J::parse(r.body.empty() ? "{}" : r.body);
  const std::string& p = r.path;
  if (p == "/api/tree") respond(fd, 200, "application/json", "{\"root\":" + jq(g_root) + ",\"tree\":" + project_json(g_root) + "}");
  else if (p == "/api/run") {   // {target: uid | group id | "", kind: cell | group, origin}
    std::string target = b["target"].str();
    std::vector<Src> cells;
    Src one;
    if (b["kind"].str() == "cell") { if (find_cell(g_root, target, one)) cells.push_back(one); }
    else cells = cells_under(g_root, target);
    run_cells(fd, cells, b["origin"].str("api"));
  }
  else if (p == "/api/sync") {   // {upto: uid | ""}: run exactly what is needed for a consistent state
    Plan pl;
    {
      std::lock_guard<std::mutex> l(g_kmu);
      pl = make_plan(b["upto"].str());
      if (pl.restart) { g_k.stop(); g_k.start(); g_hist.clear(); g_hist_unknown = false; }
      for (int i = 0; i < pl.undo; ++i) { J r = g_k.call("undo", "", [](const std::string&, const std::string&) {}); if (r["ok"].b && !g_hist.empty()) g_hist.pop_back(); }
    }
    if (pl.restart || pl.undo) publish("{\"ev\":\"rollback\",\"undo\":" + std::to_string(pl.undo) + ",\"restart\":" + (pl.restart ? "true" : "false") + ",\"origin\":" + jq(b["origin"].str("api")) + "}");
    if (pl.run.empty()) { Stream st(fd, "application/x-ndjson"); st.chunk("{\"ev\":\"uptodate\"}\n"); }
    else run_cells(fd, pl.run, b["origin"].str("api"));
  }
  else if (p == "/api/preview") {   // compile everything (without running) to learn each cell's value type and layout
    std::string out;
    {
      std::lock_guard<std::mutex> l(g_kmu);
      int id = g_k.next_id++;
      g_k.call("exec", "\"id\":" + std::to_string(id) + ",\"preview\":true,\"cells\":" + cells_json(cells_under(g_root, "")),
               [&](const std::string& k, const std::string& d) { if (k == "ev" && d.find("\"ev\":\"preview\"") != std::string::npos) out = d; });
    }
    respond(fd, 200, "application/json", out.empty() ? "{\"types\":{}}" : out);
  }
  else if (p == "/api/git") {   // branch, file status, and changed line ranges vs HEAD (for change bars on cells)
    namespace fs = std::filesystem;
    std::string base = (fs::is_directory(g_root) ? fs::absolute(g_root) : fs::absolute(g_root).parent_path()).lexically_normal().string();
    Proc top = run_cmd({"git", "-C", base, "rev-parse", "--show-toplevel"});
    if (top.status != 0) { respond(fd, 200, "application/json", "{\"repo\":false}"); close(fd); return; }
    std::string root = top.out.substr(0, top.out.find('\n'));
    std::string branch = run_cmd({"git", "-C", base, "rev-parse", "--abbrev-ref", "HEAD"}).out;
    branch = branch.substr(0, branch.find('\n'));
    std::string files, changed;
    std::istringstream st(run_cmd({"git", "-C", base, "status", "--porcelain=v1", "-uall", "--", "."}).out);
    for (std::string l; std::getline(st, l);) if (l.size() > 3) files += (files.empty() ? "" : ",") + jq(root + "/" + l.substr(3)) + ":" + jq(l.substr(0, 2));
    std::istringstream df(run_cmd({"git", "-C", base, "diff", "-U0", "HEAD", "--", "."}).out);
    std::string cur;
    for (std::string l; std::getline(df, l);) {
      if (l.rfind("+++ b/", 0) == 0) { if (!cur.empty()) changed += "],"; cur = root + "/" + l.substr(6); changed += jq(cur) + ":["; }
      else if (l.rfind("@@", 0) == 0 && !cur.empty()) {   // @@ -a,b +c,d @@
        size_t p2 = l.find('+'); int c = std::atoi(l.c_str() + p2 + 1), d = 1;
        size_t comma = l.find(',', p2); if (comma != std::string::npos && comma < l.find(' ', p2)) d = std::atoi(l.c_str() + comma + 1);
        if (changed.back() != '[') changed += ",";
        changed += "[" + std::to_string(c) + "," + std::to_string(d) + "]";
      }
    }
    if (!cur.empty()) changed += "]";
    respond(fd, 200, "application/json", "{\"repo\":true,\"branch\":" + jq(branch) + ",\"files\":{" + files + "},\"changed\":{" + changed + "}}");
  }
  else if (p == "/api/new") {   // empty folder: write the starter notebook
    namespace fs = std::filesystem;
    fs::path base = fs::is_directory(g_root) ? fs::path(g_root) : fs::path(g_root).parent_path();
    bool ok = !fs::exists(base / "main.cpp") && write_file((base / "main.cpp").string(), kStarter);
    respond(fd, 200, "application/json", std::string("{\"ok\":") + (ok ? "true" : "false") + "}");
  }
  else if (p == "/api/plan") {   // what a sync would do, for showing which cells are out of date
    Plan pl; { std::lock_guard<std::mutex> l(g_kmu); pl = make_plan(""); }
    std::string d; for (auto& u : pl.dirty) d += (d.empty() ? "" : ",") + jq(u);
    respond(fd, 200, "application/json", "{\"dirty\":[" + d + "],\"undo\":" + std::to_string(pl.undo) + ",\"restart\":" + (pl.restart ? "true" : "false") + "}");
  }
  else if (p == "/api/events") {   // every run's events, from any client, as they happen
    Stream st(fd, "application/x-ndjson");
    long seq = std::atol(r.q["since"].c_str());
    { std::lock_guard<std::mutex> l(g_rmu); if (seq == 0) seq = g_seq; }
    st.chunk("{\"ev\":\"hello\",\"seq\":" + std::to_string(seq) + "}\n");
    while (st.ok) {
      std::vector<std::string> out;
      {
        std::unique_lock<std::mutex> l(g_rmu);
        g_lcv.wait_for(l, std::chrono::seconds(15), [&] { return g_seq > seq; });
        for (auto& [n, line] : g_log) if (n > seq) out.push_back(line);
        seq = g_seq;
      }
      if (out.empty()) st.chunk("{\"ev\":\"ping\"}\n");
      for (auto& o : out) st.chunk(o + "\n");
    }
  }
  else if (p == "/api/results") {   // {uid} -> text of its last run, plus the batch id
    std::string uid = b["uid"].str();
    std::string key = key_of(uid);
    int batch; { std::lock_guard<std::mutex> l(g_rmu); batch = g_batch_of.count(key) ? g_batch_of[key] : 0; }
    respond(fd, 200, "application/json", "{\"batch\":" + std::to_string(batch) + ",\"text\":" + jq(results_text(uid)) + "}");
  }
  else if (p == "/api/outline") respond(fd, 200, "application/json", "{\"text\":" + jq(outline_text(g_root, 100000)) + "}");
  else if (p == "/api/cellsrc") { Src c; bool ok = find_cell(g_root, b["uid"].str(), c); respond(fd, ok ? 200 : 404, "application/json", ok ? "{\"code\":" + jq(c.code) + ",\"file\":" + jq(c.file) + ",\"line\":" + std::to_string(c.line) + "}" : "{}"); }
  else if (p == "/api/files") {   // list or read/write files under the project folder
    namespace fs = std::filesystem;
    fs::path base = fs::is_directory(g_root) ? fs::absolute(g_root) : fs::absolute(g_root).parent_path();
    base = base.lexically_normal();
    std::string rel = b["path"].str();
    fs::path p2 = (base / rel).lexically_normal();
    if (p2.string().rfind(base.string(), 0) != 0) { respond(fd, 403, "application/json", "{\"error\":\"outside the project\"}"); close(fd); return; }
    std::string action = b["action"].str("list");
    if (action == "read") respond(fd, 200, "application/json", "{\"text\":" + jq(read_file(p2.string())) + "}");
    else if (action == "write" && is_readonly(g_root, p2.string())) respond(fd, 403, "application/json", "{\"error\":\"read-only (see .zeno.json)\"}");
    else if (action == "write") { fs::create_directories(p2.parent_path()); bool ok = write_file(p2.string(), b["text"].str()); respond(fd, 200, "application/json", std::string("{\"ok\":") + (ok ? "true" : "false") + "}"); }
    else {
      std::string o; std::error_code ec;
      for (auto it = fs::recursive_directory_iterator(fs::is_directory(p2) ? p2 : base, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::string n = it->path().filename().string();
        if (n[0] == '.' || n == "build" || n == "node_modules") { if (it->is_directory()) it.disable_recursion_pending(); continue; }
        o += fs::relative(it->path(), base).string() + (it->is_directory() ? "/" : "  (" + std::to_string(it->is_regular_file() ? fs::file_size(it->path(), ec) : 0) + " B)") + "\n";
        if (o.size() > 60000) break;
      }
      respond(fd, 200, "application/json", "{\"text\":" + jq(o) + "}");
    }
  }
  else if (p == "/api/config") {
    Model m = model_config();
    if (r.method == "POST" && b.has("url")) { m = {b["url"].str(), b["model"].str(), b.has("key") ? b["key"].str() : m.key}; save_model_config(m); }
    respond(fd, 200, "application/json", "{\"url\":" + jq(m.url) + ",\"model\":" + jq(m.model) + ",\"has_key\":" + (m.key.empty() ? "false" : "true") + "}");
  }
  else if (p == "/api/chat") {   // {messages: [{role, content}], focus: uid}; streams {ev: delta, s}
    Stream st(fd, "application/x-ndjson");
    std::string msgs = "[{\"role\":\"system\",\"content\":" + jq(chat_system(b["focus"].str())) + "}";
    for (auto& m : b["messages"].a) msgs += ",{\"role\":" + jq(m["role"].str()) + ",\"content\":" + jq(m["content"].str()) + "}";
    bool told = false;
    chat(msgs + "]", [&](const std::string& d) { st.chunk("{\"ev\":\"delta\",\"s\":" + jq(d) + "}\n"); }, 0, true,
         [&] { if (!told) { told = true; st.chunk("{\"ev\":\"thinking\"}\n"); } });
  }
  else if (p == "/api/complete") {   // {uid, prefix, suffix} -> {items}
    std::string ctx;
    for (auto& c : cells_under(g_root, "")) { if (c.uid == b["uid"].str()) break; ctx += c.code + "\n"; }
    std::string items;
    for (auto& i : complete(ctx, b["prefix"].str(), b["suffix"].str())) items += (items.empty() ? "" : ",") + jq(i);
    respond(fd, 200, "application/json", "{\"items\":[" + items + "]}");
  }
  else if (p == "/api/watch") {   // long poll: returns as soon as the files differ from version `v`
    long since = std::atol(r.q["v"].c_str());
    std::unique_lock<std::mutex> l(g_vmu);
    g_vcv.wait_for(l, std::chrono::seconds(25), [&] { return g_version != since; });
    respond(fd, 200, "application/json", "{\"v\":" + std::to_string(g_version.load()) + "}");
  }
  else if (p == "/api/op") {
    std::string o = b["op"].str();
    if (o == "undo") {   // keep the run history in step with the kernel's snapshots
      Stream st(fd, "application/x-ndjson");
      std::lock_guard<std::mutex> l(g_kmu);
      J r = g_k.call("undo", "", [&](const std::string& k, const std::string& d) { if (k == "ev") st.chunk(d + "\n"); });
      if (r["ok"].b && !g_hist.empty()) g_hist.pop_back();
    } else kernel_stream(fd, o, fields_of(b, {"op"}));
  }
  else if (p == "/api/interrupt") { g_k.interrupt(); respond(fd, 200, "application/json", "{}"); }
  else if (p == "/api/restart") {
    std::lock_guard<std::mutex> l(g_kmu);
    g_k.stop(); g_k.start(); g_hist.clear(); g_hist_unknown = false;
    respond(fd, 200, "application/json", "{}");
  }
  else if (p == "/api/cell") {   // {action: replace|insert_after|delete, uid, text}
    std::string a = b["action"].str();
    bool ok = replace_cell(g_root, b["uid"].str(), a == "delete" ? "" : b["text"].str(), a == "insert_after");
    respond(fd, ok ? 200 : 400, "application/json", "{\"ok\":" + std::string(ok ? "true" : "false") + (ok ? "" : ",\"error\":\"no such cell, or the file is read-only\"") + "}");
  }
  else if (p == "/api/extract") {   // move cells into a new file, leaving an #include where the first one was
    std::vector<std::pair<Src, Item>> cells;
    for (auto& u : b["uids"].a) { Src s; Item it; if (find_cell(g_root, u.str(), s, &it)) cells.push_back({s, it}); }
    std::string name = b["file"].str();
    if (cells.empty() || name.empty() || name.find("..") != std::string::npos) { respond(fd, 400, "application/json", "{\"ok\":false}"); close(fd); return; }
    std::string dir = cells[0].first.file.substr(0, cells[0].first.file.rfind('/') + 1), text = read_file(dir + name);
    for (auto& [s, it] : cells) text += (text.empty() ? "" : "\n") + s.code + "\n";
    std::filesystem::create_directories(std::filesystem::path(dir + name).parent_path());
    write_file(dir + name, text);
    // delete from the last cell backwards so earlier indices stay valid, then replace the first with the include
    for (size_t i = cells.size(); i-- > 1;) replace_cell(g_root, cells[i].first.uid, "");
    replace_cell(g_root, cells[0].first.uid, "#include \"" + name + "\"");
    respond(fd, 200, "application/json", "{\"ok\":true}");
  }
  else if (p == "/api/term/open") {
    int id = open_term(b["uid"].str(), (int)b["rows"].num(8), (int)b["cols"].num(100));
    respond(fd, id > 0 ? 200 : 400, "application/json", "{\"id\":" + std::to_string(id) + "}");
  }
  else if (p == "/api/term/out") { if (auto t = term(r.q["id"])) term_stream(fd, t, std::atoi(r.q["id"].c_str())); else respond(fd, 404, "text/plain", ""); }
  else if (p == "/api/term/in") {
    if (getenv("ZENO_DEBUG")) std::fprintf(stderr, "term in: %s\n", jq(r.body).c_str());
    if (auto t = term(r.q["id"])) (void)!write(t->master, r.body.data(), r.body.size());
    respond(fd, 200, "text/plain", "");
  }
  else if (p == "/api/term/size") {
    if (auto t = term(b["id"].dump())) { winsize ws{(unsigned short)std::max(5.0, b["rows"].num(8)), (unsigned short)std::max(20.0, b["cols"].num(100)), 0, 0}; ioctl(t->master, TIOCSWINSZ, &ws); }
    respond(fd, 200, "text/plain", "");
  }
  else respond(fd, 404, "text/plain", "not found");
  close(fd);
}
}  // namespace

int web_main(const std::string& root, int port, bool open_browser, const std::string& host, bool token) {
  signal(SIGPIPE, SIG_IGN);
  g_root = root.empty() ? "." : root;
  if (token) {   // --token: every request must carry a random secret
    std::random_device rd;
    char tk[33]; std::snprintf(tk, sizeof tk, "%08x%08x%08x%08x", rd(), rd(), rd(), rd());
    g_token = tk;
  }
  g_k.mode = "web"; g_k.start();
  int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{}; a.sin_family = AF_INET;
  if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) { std::fprintf(stderr, "bad --host %s (want an IPv4 address such as 0.0.0.0)\n", host.c_str()); return 1; }
  for (int tries = 0;; ++tries, ++port) {
    a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof a) == 0) break;
    if (tries > 20) { perror("bind"); return 1; }
  }
  listen(s, 64);
  std::thread([] {   // poll the project for edits made outside the page (editors, coding agents, git)
    size_t sig = project_signature(g_root);
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      size_t now = project_signature(g_root);
      if (now != sig) { sig = now; { std::lock_guard<std::mutex> l(g_vmu); ++g_version; } g_vcv.notify_all(); }
    }
  }).detach();
  g_port = port;
  std::string q = g_token.empty() ? "/" : "/?token=" + g_token;
  std::string url = "http://127.0.0.1:" + std::to_string(port) + q;
  {   // advertise this server so `zeno mcp` can find it
    std::string dir = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.cache/zeno/servers";
    std::filesystem::create_directories(dir);
    std::error_code ec;   // forget servers that died without cleaning up
    for (auto& e : std::filesystem::directory_iterator(dir, ec))
      if (kill(std::atoi(e.path().stem().c_str()), 0) != 0) std::filesystem::remove(e.path(), ec);
    g_server_file = dir + "/" + std::to_string(getpid()) + ".json";
    write_file(g_server_file, "{\"pid\":" + std::to_string(getpid()) + ",\"port\":" + std::to_string(port) + ",\"token\":" + jq(g_token) +
                                  ",\"root\":" + jq(std::filesystem::absolute(g_root).lexically_normal().string()) + "}\n");
  }
  std::printf("Zeno web: %s\n", url.c_str());
  if (host != "127.0.0.1") {   // list the addresses other machines can use
    std::vector<std::string> ips;
    if (host != "0.0.0.0") ips.push_back(host);
    else if (ifaddrs* ifs; getifaddrs(&ifs) == 0) {
      for (ifaddrs* i = ifs; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !(i->ifa_flags & IFF_LOOPBACK)) {
          char b[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, b, sizeof b); ips.push_back(b);
        }
      freeifaddrs(ifs);
    }
    for (auto& ip : ips) std::printf("          http://%s:%d%s\n", ip.c_str(), port, q.c_str());
    std::printf(g_token.empty() ? "  anyone who can reach these addresses can run code as you (--token requires a secret)\n"
                                : "  anyone who can reach these addresses and has the token can run code as you\n");
  }
  std::printf("  serving %s — Ctrl-C to stop\n", std::filesystem::absolute(g_root).c_str());
  std::fflush(stdout);
  if (open_browser && (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY"))) { if (fork() == 0) { int n = open("/dev/null", 1); dup2(n, 1); dup2(n, 2); execlp("xdg-open", "xdg-open", url.c_str(), (char*)nullptr); _exit(0); } }
  static Kernel* kp = &g_k;
  std::atexit([] { kp->stop(); unlink(g_server_file.c_str()); });
  signal(SIGINT, [](int) { exit(0); });
  signal(SIGTERM, [](int) { exit(0); });
  for (;;) {
    int c = accept(s, nullptr, nullptr);
    if (c < 0) continue;
    std::thread(handle, c).detach();
  }
}
