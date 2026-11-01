// A notebook is a folder of ordinary C++ files (or a single file). Folders, files and markdown
// headings inside comments form the collapsible hierarchy; code statements are the cells.
#include "common.hpp"
#include <algorithm>
#include <filesystem>
#include <fnmatch.h>
#include <functional>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace {
bool natural_less(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
      size_t x = i, y = j;
      while (x < a.size() && std::isdigit((unsigned char)a[x])) ++x;
      while (y < b.size() && std::isdigit((unsigned char)b[y])) ++y;
      long long u = std::stoll(a.substr(i, std::min<size_t>(x - i, 18))), v = std::stoll(b.substr(j, std::min<size_t>(y - j, 18)));
      if (u != v) return u < v;
      i = x; j = y;
    } else {
      if (a[i] != b[j]) return a[i] < b[j];
      ++i; ++j;
    }
  }
  return a.size() - i < b.size() - j;
}
bool source_file(const fs::path& p) {
  static const std::vector<std::string> ext = {".cpp", ".cc", ".cxx", ".hpp", ".h", ".hh", ".hxx", ".ipp", ".inl"};
  return std::find(ext.begin(), ext.end(), p.extension().string()) != ext.end();
}
bool skip_dir(const std::string& n) { return n.empty() || n[0] == '.' || n == "build" || n == "node_modules" || n.rfind("cmake-build", 0) == 0; }
std::string title_of(const std::string& name) {   // "02_load-data.cpp" -> "load-data.cpp"
  static const std::regex pre(R"(^\d+[ _.\-]+)");
  std::string t = std::regex_replace(name, pre, "");
  return t.empty() ? name : t;
}
fs::path base_of(const fs::path& root) { return fs::is_directory(root) ? root : root.parent_path(); }
std::string rel(const fs::path& root, const fs::path& p) { return fs::relative(p, base_of(root)).string(); }
fs::path entry_of(const fs::path& root) {   // a file, or a folder's main.cpp; empty: just browse the folder
  if (!fs::is_directory(root)) return root;
  for (auto n : {"main.cpp", "main.cc", "notebook.cpp"}) if (fs::exists(root / n)) return root / n;
  return {};
}
// `#include "x"` resolved against the including file's folder, then the project folder and its include/.
fs::path local_include(const fs::path& root, const fs::path& from, const std::string& code) {
  static const std::regex inc(R"re(^\s*#\s*include\s*"([^"]+)")re");
  std::smatch m;
  if (!std::regex_search(code, m, inc)) return {};
  for (auto d : {from.parent_path(), base_of(root), base_of(root) / "include"})
    if (fs::exists(d / m[1].str())) return fs::weakly_canonical(d / m[1].str());
  return {};
}
// Include guards and #pragma once are noise in a notebook view; drop them from the item list.
std::vector<Item> items_of(const fs::path& f) {
  auto v = scan_source(read_file(f.string()));
  std::vector<size_t> code;
  for (size_t i = 0; i < v.size(); ++i) if (v[i].kind == "code") code.push_back(i);
  auto starts = [&](size_t i, const char* p) { return v[i].text.rfind(p, 0) == 0; };
  std::vector<bool> drop(v.size());
  for (size_t i : code) if (starts(i, "#pragma once")) drop[i] = true;
  if (code.size() >= 3 && starts(code[0], "#ifndef") && starts(code[1], "#define") && starts(code.back(), "#endif")) {
    std::string g = v[code[0]].text.substr(7); g.erase(0, g.find_first_not_of(" \t"));
    if (v[code[1]].text.find(g) != std::string::npos) drop[code[0]] = drop[code[1]] = drop[code.back()] = true;
  }
  std::vector<Item> out;
  for (size_t i = 0; i < v.size(); ++i) if (!drop[i]) out.push_back(v[i]);
  return out;
}

std::vector<fs::path> children(const fs::path& d) {
  std::vector<fs::path> v;
  for (auto& e : fs::directory_iterator(d)) {
    std::string n = e.path().filename().string();
    if (e.is_directory() ? !skip_dir(n) : source_file(e.path())) v.push_back(e.path());
  }
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return natural_less(a.filename().string(), b.filename().string()); });
  return v;
}
void all_files(const fs::path& p, std::vector<fs::path>& out) {
  if (!fs::is_directory(p)) { out.push_back(p); return; }
  for (auto& c : children(p)) all_files(c, out);
}

std::string file_json(const fs::path& root, const fs::path& f, std::set<fs::path>& seen, bool follow) {
  std::string r = rel(root, f);
  seen.insert(fs::weakly_canonical(f));
  auto items = items_of(f);
  // a stack of open sections; each holds its JSON children so far
  // Every node carries its byte range [b, e) in the file, so the page can edit a comment, a section or a function as text.
  struct Sec { int level; std::string head, kids; size_t b; };
  std::vector<Sec> st{{0, "", "", 0}};
  auto add = [&](const std::string& j) { auto& k = st.back().kids; k += (k.empty() ? "" : ",") + j; };
  auto range = [](size_t b, size_t e) { return ",\"b\":" + std::to_string(b) + ",\"e\":" + std::to_string(e); };
  auto close = [&](size_t e) { Sec s = st.back(); st.pop_back(); add(s.head + s.kids + "]" + range(s.b, std::max(e, s.b)) + "}"); };
  auto prev_e = [&](size_t i) { return i ? items[i - 1].e : 0; };
  int code = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    auto& it = items[i];
    if (it.kind == "fn") {   // main(): a group titled by its signature; sections inside it stay inside it
      st.push_back({1000, "{\"k\":\"fn\",\"id\":" + jq(r + "#" + std::to_string(it.line)) + ",\"title\":" + jq(it.text) + ",\"line\":" + std::to_string(it.line) + ",\"kids\":[", "", it.b});
      continue;
    }
    if (it.kind == "endfn") { while (st.size() > 1 && st.back().level != 1000) close(prev_e(i)); if (st.size() > 1) close(it.e); continue; }
    if (it.kind == "md" && it.level > 0) {
      while (st.size() > 1 && st.back().level >= it.level && st.back().level != 1000) close(prev_e(i));
      size_t nl = it.text.find('\n');
      std::string title = it.text.substr(it.level, nl == std::string::npos ? std::string::npos : nl - it.level);
      title.erase(0, title.find_first_not_of(' '));
      st.push_back({it.level, "{\"k\":\"sec\",\"id\":" + jq(r + "@" + std::to_string(i)) + ",\"title\":" + jq(title) + ",\"level\":" + std::to_string(it.level) + ",\"kids\":[", "", it.b});
      std::string rest = nl == std::string::npos ? "" : it.text.substr(nl + 1);
      rest.erase(0, rest.find_first_not_of('\n'));
      if (!rest.empty()) add("{\"k\":\"md\",\"text\":" + jq(rest) + range(it.b, it.e) + "}");
      continue;
    }
    fs::path inc = follow && it.kind == "code" ? local_include(root, f, it.text) : fs::path();
    if (!inc.empty()) {   // a local include is a collapsible group holding that file's cells
      std::string uid = r + ":" + std::to_string(code++);
      if (seen.count(inc)) add("{\"k\":\"inc\",\"uid\":" + jq(uid) + ",\"text\":" + jq(it.text) + ",\"line\":" + std::to_string(it.line) + ",\"dup\":true,\"id\":" + jq(rel(root, inc)) + "}");
      else {
        std::string sub = file_json(root, inc, seen, true);
        add("{\"k\":\"inc\",\"uid\":" + jq(uid) + ",\"text\":" + jq(it.text) + ",\"line\":" + std::to_string(it.line) + ",\"file\":" + sub + "}");
      }
      continue;
    }
    if (it.kind == "code")
      add("{\"k\":\"code\",\"uid\":" + jq(r + ":" + std::to_string(code++)) + ",\"line\":" + std::to_string(it.line) + ",\"text\":" + jq(it.text) + range(it.b, it.e) + "}");
    else add("{\"k\":" + jq(it.kind) + ",\"line\":" + std::to_string(it.line) + ",\"text\":" + jq(it.text) + range(it.b, it.e) + "}");
  }
  while (st.size() > 1) close(items.empty() ? 0 : items.back().e);
  return "{\"k\":\"file\",\"id\":" + jq(r) + ",\"title\":" + jq(title_of(f.filename().string())) + ",\"path\":" + jq(fs::absolute(f).string()) +
         (is_readonly(root.string(), f.string()) ? ",\"ro\":true" : "") + range(0, fs::file_size(f)) + ",\"kids\":[" + st[0].kids + "]}";
}
std::string node_json(const fs::path& root, const fs::path& p) {
  std::set<fs::path> seen;
  if (!fs::is_directory(p)) return file_json(root, p, seen, false);
  std::string kids;
  for (auto& c : children(p)) kids += (kids.empty() ? "" : ",") + node_json(root, c);
  return "{\"k\":\"dir\",\"id\":" + jq(p == root ? "" : rel(root, p)) + ",\"title\":" + jq(title_of(p.filename().string())) + ",\"kids\":[" + kids + "]}";
}
fs::path file_of(const std::string& root, const std::string& r) { return base_of(fs::absolute(root).lexically_normal()) / r; }

// Cells of a file in run order, with local includes expanded in place (each file at most once).
void expand(const fs::path& root, const fs::path& f, std::set<fs::path>& seen, int from_sec, std::vector<Src>& out) {
  if (!seen.insert(fs::weakly_canonical(f)).second) return;
  auto items = items_of(f);
  std::string r = rel(root, f);
  int n = 0, level = 0; bool in = from_sec < 0;
  for (size_t i = 0; i < items.size(); ++i) {
    auto& it = items[i];
    if (from_sec >= 0 && (int)i == from_sec) { in = true; level = it.level; continue; }
    if (from_sec >= 0 && in && (int)i > from_sec && it.kind == "md" && it.level > 0 && it.level <= level) in = false;
    if (it.kind == "fn" && in && it.text.find("argc") != std::string::npos)   // give main's parameters notebook values
      out.push_back({r + ":args", "int argc = 1;\nchar* __zeno_argv[] = {(char*)\"" + r + "\", nullptr};\nchar** argv = __zeno_argv;", fs::absolute(f).string(), it.line});
    if (it.kind != "code") continue;
    int idx = n++;
    if (!in) continue;
    fs::path inc = local_include(root, f, it.text);
    if (!inc.empty()) expand(root, inc, seen, -1, out);
    else out.push_back({r + ":" + std::to_string(idx), it.text, fs::absolute(f).string(), it.line});
  }
}
}  // namespace

size_t project_signature(const std::string& root) {
  std::vector<fs::path> files;
  std::error_code ec;
  fs::path p = fs::absolute(root).lexically_normal();
  all_files(base_of(p), files);
  size_t h = files.size();
  for (auto& f : files) {
    auto t = fs::last_write_time(f, ec).time_since_epoch().count();
    h = h * 1000003 ^ std::hash<std::string>{}(f.string()) ^ (size_t)t ^ (size_t)fs::file_size(f, ec) << 1;
  }
  return h;
}

std::string project_json(const std::string& root) {
  fs::path p = fs::absolute(root).lexically_normal();
  if (!fs::exists(p)) return "{\"k\":\"dir\",\"id\":\"\",\"title\":" + jq(root) + ",\"kids\":[]}";
  fs::path entry = entry_of(p);
  if (entry.empty()) return node_json(p, p);
  std::set<fs::path> seen;
  std::string main = file_json(p, entry, seen, true), others;
  std::vector<fs::path> all;   // files the entry point doesn't reach are still browsable
  all_files(base_of(p), all);
  for (auto& f : all) if (!seen.count(fs::weakly_canonical(f))) { std::set<fs::path> s2; others += (others.empty() ? "" : ",") + file_json(p, f, s2, false); }
  return "{\"k\":\"entry\",\"main\":" + main + ",\"others\":[" + others + "]}";
}

bool find_cell(const std::string& root, const std::string& uid, Src& out, Item* item) {
  size_t c = uid.rfind(':');
  if (c == std::string::npos) return false;
  fs::path f = file_of(root, uid.substr(0, c));
  int want = std::atoi(uid.c_str() + c + 1), n = 0;
  for (auto& it : items_of(f)) {
    if (it.kind != "code") continue;
    if (n++ == want) { out = {uid, it.text, fs::absolute(f).string(), it.line}; if (item) *item = it; return true; }
  }
  return false;
}

std::vector<Src> cells_under(const std::string& root, const std::string& prefix) {
  fs::path p = fs::absolute(root).lexically_normal();
  std::vector<Src> out;
  std::set<fs::path> seen;
  std::string pre = prefix; size_t at = pre.find('@'); int sec = -1;
  if (at != std::string::npos) { sec = std::atoi(pre.c_str() + at + 1); pre = pre.substr(0, at); }
  if (pre.empty()) {
    fs::path e = entry_of(p);
    if (!e.empty()) { expand(p, e, seen, -1, out); return out; }
  }
  std::vector<fs::path> files;
  all_files(base_of(p), files);
  for (auto& f : files) {
    std::string r = rel(p, f);
    if (pre.empty() || r == pre || r.rfind(pre + "/", 0) == 0) expand(p, f, seen, sec, out);
  }
  return out;
}

bool is_readonly(const std::string& root, const std::string& file) {
  fs::path base = base_of(fs::absolute(root).lexically_normal());
  J cfg = J::parse(read_file((base / ".zeno.json").string()).empty() ? "{}" : read_file((base / ".zeno.json").string()));
  std::string r = fs::relative(fs::absolute(file).lexically_normal(), base).string();
  for (auto& g : cfg["readonly"].a) {
    std::string pat = g.str();
    if (pat.size() > 3 && pat.ends_with("/**") && (r == pat.substr(0, pat.size() - 3) || r.rfind(pat.substr(0, pat.size() - 2), 0) == 0)) return true;
    if (fnmatch(pat.c_str(), r.c_str(), 0) == 0) return true;
  }
  return false;
}

bool replace_cell(const std::string& root, const std::string& uid, const std::string& text, bool insert_after) {
  Src s; Item it;
  if (!find_cell(root, uid, s, &it)) return false;
  if (is_readonly(root, s.file)) return false;
  std::string body = read_file(s.file);
  if (insert_after) body.insert(it.e, "\n\n" + text);
  else if (text.empty()) {   // delete the cell and the blank line it leaves behind
    size_t e = it.e; while (e < body.size() && (body[e] == '\n' || body[e] == ' ')) { bool nl = body[e] == '\n'; ++e; if (nl) break; }
    body.erase(it.b, e - it.b);
  } else body.replace(it.b, it.e - it.b, text);
  return write_file(s.file, body);
}

// Edit bytes [b, e) of a project file as text, if they still read `orig`. An empty range inserts a new
// paragraph there; empty text deletes the range and the blank line it leaves.
bool replace_region(const std::string& root, const std::string& file, size_t b, size_t e, const std::string& orig, const std::string& text) {
  fs::path base = base_of(fs::absolute(root).lexically_normal()), f = fs::absolute(file).lexically_normal();
  if (f.string().rfind(base.string(), 0) != 0 || is_readonly(root, f.string())) return false;
  std::string body = read_file(f.string());
  if (b > e || e > body.size() || body.compare(b, e - b, orig) != 0) return false;   // changed underneath us
  if (b == e) { if (text.empty()) return true; body.insert(b, b == 0 ? text + "\n\n" : "\n\n" + text); }
  else if (text.empty()) { while (e < body.size() && (body[e] == '\n' || body[e] == ' ')) { bool nl = body[e] == '\n'; ++e; if (nl) break; } body.erase(b, e - b); }
  else body.replace(b, e - b, text);
  return write_file(f.string(), body);
}
std::string read_region(const std::string& root, const std::string& file, size_t b, size_t e) {
  fs::path base = base_of(fs::absolute(root).lexically_normal()), f = fs::absolute(file).lexically_normal();
  if (f.string().rfind(base.string(), 0) != 0) return "";
  std::string body = read_file(f.string());
  return b <= e && e <= body.size() ? body.substr(b, e - b) : "";
}

// A compact outline for prompts and agents: headings, includes, and each cell's uid with its first line.
std::string outline_text(const std::string& root, size_t limit) {
  std::string o;
  std::function<void(const J&, int)> walk = [&](const J& n, int d) {
    if (o.size() > limit) return;
    std::string ind(2 * d, ' '), k = n["k"].str();
    if (k == "entry") { walk(n["main"], 0); if (!n["others"].a.empty()) { o += "(files not included from the entry point)\n"; for (auto& f : n["others"].a) walk(f, 1); } }
    else if (k == "file" || k == "dir") { o += ind + (k == "dir" ? "dir " : "file ") + n["id"].str() + "\n"; for (auto& c : n["kids"].a) walk(c, d + 1); }
    else if (k == "sec") { o += ind + std::string(n["level"].num(), '#') + " " + n["title"].str() + "\n"; for (auto& c : n["kids"].a) walk(c, d + 1); }
    else if (k == "inc") { o += ind + "[" + n["uid"].str() + "] " + n["text"].str() + "\n"; if (n.has("file")) for (auto& c : n["file"]["kids"].a) walk(c, d + 1); }
    else if (k == "code") {
      std::string t = n["text"].str(); size_t nl = t.find('\n');
      o += ind + "[" + n["uid"].str() + "] " + (nl == std::string::npos ? t : t.substr(0, nl) + " …") + "\n";
    } else if (k == "md") { std::string t = n["text"].str(); o += ind + "// " + t.substr(0, std::min(t.find('\n'), (size_t)100)) + "\n"; }
  };
  walk(J::parse(project_json(root)), 0);
  if (o.size() > limit) o = o.substr(0, limit) + "\n… (truncated)\n";
  return o;
}
