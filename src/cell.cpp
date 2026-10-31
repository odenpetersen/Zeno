// Splits a cell into top-level chunks, classifies them, and generates the cell's translation unit.
//
// Every cell lives in its own namespace nested inside all previous ones, so unqualified lookup
// finds the newest declaration (that is what makes redeclaration work). Variables become
// `inline` namespace-scope variables: later cells re-include their definitions, and the dynamic
// linker binds them (and their init guards) to the instance in the defining cell's .so.
// Statements become ordered static initializers, so a cell runs top to bottom when dlopen'ed.
#include "common.hpp"
#include <cctype>
#include <algorithm>
#include <regex>
#include <set>
#include <sstream>

namespace {
struct Chunk { std::string text; int line; bool term; size_t b = 0, e = 0; };

bool idstart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool idchar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Skip a string/char/comment starting at i; returns the index after it, or i if none starts there.
size_t skip_lit(const std::string& s, size_t i) {
  size_t n = s.size();
  if (s.compare(i, 2, "//") == 0) { while (i < n && s[i] != '\n') ++i; return i; }
  if (s.compare(i, 2, "/*") == 0) { size_t e = s.find("*/", i + 2); return e == std::string::npos ? n : e + 2; }
  if (s[i] == 'R' && i + 1 < n && s[i + 1] == '"' && (i == 0 || !idchar(s[i - 1]))) {
    size_t p = s.find('(', i); if (p == std::string::npos) return n;
    std::string end = ")" + s.substr(i + 2, p - i - 2) + "\"";
    size_t e = s.find(end, p); return e == std::string::npos ? n : e + end.size();
  }
  if (s[i] == '"' || (s[i] == '\'' && (i == 0 || !std::isxdigit((unsigned char)s[i - 1])))) {
    char q = s[i++];
    while (i < n && s[i] != q) { if (s[i] == '\\') ++i; ++i; }
    return std::min(n, i + 1);
  }
  return i;
}
size_t skip_ws(const std::string& s, size_t i) {
  for (;;) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i < s.size() && (s.compare(i, 2, "//") == 0 || s.compare(i, 2, "/*") == 0)) i = skip_lit(s, i);
    else return i;
  }
}
std::string word_at(const std::string& s, size_t i) { size_t j = i; while (j < s.size() && idchar(s[j])) ++j; return s.substr(i, j - i); }

std::vector<std::string> tokens(const std::string& s, size_t max = 400) {
  std::vector<std::string> t;
  for (size_t i = skip_ws(s, 0); i < s.size() && t.size() < max; i = skip_ws(s, i)) {
    if (s[i] == '#') { while (i < s.size() && s[i] != '\n') ++i; continue; }   // attached #pragma
    size_t j = skip_lit(s, i);
    if (j != i) { t.push_back("\"\""); i = j; continue; }
    if (idstart(s[i])) { t.push_back(word_at(s, i)); i += t.back().size(); continue; }
    if (std::isdigit((unsigned char)s[i])) { j = i; while (j < s.size() && (idchar(s[j]) || s[j] == '.' || s[j] == '\'')) ++j; t.push_back("0"); i = j; continue; }
    static const char* ops[] = {"::", "<<", ">>", "->", "&&", "||", "==", "!=", "<=", ">=", "++", "--", "+=", "-=", "*=", "/="};
    std::string op(1, s[i]);
    for (auto o : ops) if (s.compare(i, 2, o) == 0) op = o;
    t.push_back(op); i += op.size();
  }
  return t;
}

// Split source into top-level chunks ending in ';' or a closing '}' (function bodies, blocks).
std::vector<Chunk> split(const std::string& s) {
  std::vector<Chunk> out;
  std::string pending;   // statement pragmas attach to the next chunk
  size_t pending_b = 0;
  auto line_of = [&](size_t i) { return 1 + (int)std::count(s.begin(), s.begin() + i, '\n'); };
  size_t i = 0, n = s.size();
  while ((i = skip_ws(s, i)) < n) {
    size_t st = i;
    bool linestart = st == 0 || s.rfind('\n', st - 1) == std::string::npos ||
                     s.find_first_not_of(" \t", s.rfind('\n', st - 1) + 1) == st;
    if ((s[i] == '#' || s[i] == '%') && linestart) {
      while (i < n && s[i] != '\n') { if (s[i] == '\\' && i + 1 < n && s[i + 1] == '\n') ++i; ++i; }
      std::string line = s.substr(st, i - st);
      if (line.rfind("#pragma", 0) == 0 && line.find("once") == std::string::npos && line.find("GCC optimize") == std::string::npos &&
          line.find("GCC diagnostic") == std::string::npos && line.find("GCC target") == std::string::npos)
        { if (pending.empty()) pending_b = st; pending += line + "\n"; }
      else out.push_back({line, line_of(st), true, st, i});
      continue;
    }
    int depth = 0; bool term = false;
    std::string first = word_at(s, st);
    for (; i < n; ++i) {
      size_t j = skip_lit(s, i);
      if (j != i) { i = j - 1; continue; }
      char c = s[i];
      if (c == '(' || c == '[' || c == '{') ++depth;
      else if (c == ')' || c == ']' || c == '}') --depth;
      if (depth == 0 && c == ';') { ++i; term = true; break; }
      if (depth == 0 && c == '}') {
        size_t k = skip_ws(s, i + 1);
        if (k >= n) { ++i; term = true; break; }
        char nc = s[k];
        if (nc == ';' || std::string_view(",)(.-[=?:<>+*/&|^!").find(nc) != std::string_view::npos) continue;
        std::string w = idstart(nc) ? word_at(s, k) : "";
        bool typedef_ = first == "struct" || first == "class" || first == "union" || first == "enum" || first == "typedef";
        if (w == "else" || w == "while" || w == "catch" || (!w.empty() && typedef_)) continue;
        ++i; term = true; break;
      }
    }
    std::string text = s.substr(st, std::min(i, n) - st);
    while (!text.empty() && std::isspace((unsigned char)text.back())) text.pop_back();
    out.push_back({pending + text, line_of(st) - (int)std::count(pending.begin(), pending.end(), '\n'), term, pending.empty() ? st : pending_b, st + text.size()});
    pending.clear();
  }
  return out;
}

const std::set<std::string> kDeclKw = {"struct", "class", "union", "enum", "namespace", "template", "typedef", "using",
                                       "static_assert", "extern", "concept", "friend"};
const std::set<std::string> kStmtKw = {"if", "for", "while", "do", "switch", "return", "try", "break", "continue", "goto",
                                       "throw", "delete", "co_await", "co_return", "co_yield", "case", "default", "new", "this", "sizeof"};
const std::set<std::string> kBuiltinTypes = {"int", "char", "double", "float", "long", "short", "bool", "unsigned", "signed",
                                             "void", "auto", "size_t", "const", "volatile", "string"};

// What a chunk is: 'p' preprocessor, 'm' magic, 'v' variable, 'b' structured binding,
// 'q' out-of-class static member definition, 'f' function/type/other declaration, 's' statement.
struct Kind { char k; std::string name; bool fn = false; };
Kind classify(const Chunk& c) {
  auto t = tokens(c.text);
  if (c.text[0] == '#' && t.empty()) return {'p', ""};
  if (c.text[0] == '%') return {'m', ""};
  if (t.empty()) return {'s', ""};
  if (t[0] == "[" && t.size() > 1 && t[1] == "[") {   // leading attributes
    size_t k = 2; int d = 2; while (k < t.size() && d) { d += t[k] == "[" ? 1 : t[k] == "]" ? -1 : 0; ++k; }
    t.erase(t.begin(), t.begin() + k);
    if (t.empty()) return {'s', ""};
  }
  if (kDeclKw.count(t[0])) return {'f', ""};
  if (kStmtKw.count(t[0]) || !idstart(t[0][0])) return {'s', ""};
  size_t i = 0; int ntype = 0; std::string name; bool qualified = false, lastq = false;
  while (i < t.size()) {
    const std::string& w = t[i];
    if (w == "decltype" || w == "typeof" || w == "alignas") {
      ++i; int d = 0; do { d += t[i] == "(" ? 1 : t[i] == ")" ? -1 : 0; ++i; } while (i < t.size() && d > 0);
      ++ntype; lastq = false; continue;
    }
    if (w == "operator" && ntype >= 1) return {'f', "", true};
    if (idstart(w[0]) && !kStmtKw.count(w)) {
      if (ntype > 0 && !lastq && name.empty()) {}
      name = lastq ? name + "::" + w : w;
      if (!lastq) ++ntype;
      ++i; lastq = false;
      if (i < t.size() && t[i] == "<") {   // template arguments
        int d = 0;
        for (; i < t.size(); ++i) {
          if (t[i] == "<") ++d; else if (t[i] == ">") --d; else if (t[i] == ">>") d -= 2;
          else if (t[i] == ";" || t[i] == "{") return {'s', ""};
          if (d <= 0) { ++i; break; }
        }
      }
      if (i < t.size() && t[i] == "::") { ++i; lastq = true; qualified = true; continue; }
      qualified = name.find("::") != std::string::npos;
      continue;
    }
    if ((w == "*" || w == "&" || w == "&&") && ntype >= 1) { ++i; name.clear(); continue; }
    if (w == "[" && ntype >= 1 && name == "auto") return {'b', ""};
    break;
  }
  if (ntype < 2 || name.empty()) return {'s', ""};
  std::string next = i < t.size() ? t[i] : ";";
  if (next == "(") {
    bool body = c.text.back() == '}';
    if (body) return {'f', name, true};
    // `T x(args);` is a variable unless the parentheses look like a parameter list.
    size_t k = i + 1; bool params = k < t.size() && t[k] == ")";
    if (k < t.size() && (kBuiltinTypes.count(t[k]) || (idstart(t[k][0]) && k + 1 < t.size() && (idstart(t[k + 1][0]) || t[k + 1] == "&" || t[k + 1] == "*" || t[k + 1] == "<" || t[k + 1] == "::")))) params = true;
    if (params) return {'f', name, true};
  }
  if (next == "=" || next == ";" || next == "{" || next == "(" || next == "[" || next == ",")
    return {qualified ? 'q' : 'v', name};
  return {'s', ""};
}

std::string strip_static(std::string s) {
  size_t i = skip_ws(s, 0);
  if (s.compare(i, 7, "static ") == 0) s.erase(i, 7);
  return s;
}
std::string make_inline(const std::string& s) {
  size_t i = s.find_first_not_of(" \t\n");
  while (i < s.size() && s[i] == '#') i = s.find_first_not_of(" \t\n", s.find('\n', i));   // after pragmas
  if (i >= s.size()) return s;
  std::string w = word_at(s, i);
  if (w == "inline" || w == "extern" || w == "template" || w == "typedef") return s;
  return s.substr(0, i) + "inline " + s.substr(i);
}

// Transform declarations inside a user namespace block so later cells can re-include it.
std::string fix_namespace(const std::string& text) {
  size_t a = text.find('{'), b = text.rfind('}');
  if (a == std::string::npos || b == std::string::npos || b < a) return text;
  std::string inner = text.substr(a + 1, b - a - 1), out;
  for (auto& c : split(inner)) {
    Kind k = classify(c);
    if (k.k == 'v') out += make_inline(strip_static(c.text)) + "\n";
    else if (k.k == 'f' && k.fn && c.text.back() == '}') out += make_inline(c.text) + "\n";
    else if (k.k == 'f' && c.text.compare(0, 9, "namespace") == 0) out += fix_namespace(c.text) + "\n";
    else out += c.text + "\n";
  }
  return text.substr(0, a + 1) + "\n" + out + text.substr(b);
}
}  // namespace

const std::set<std::string> kCtrl = {"if", "for", "while", "do", "switch", "return", "try", "break", "continue", "goto",
                                     "throw", "co_return", "co_yield", "case", "default", "{"};

Gen gen_batch(const std::vector<Src>& cells, int id) {
  Gen g;
  int seq = 0, cond = 0;
  std::string sid = std::to_string(id);
  auto var = [&](const char* p) { return std::string(p) + std::to_string(seq++); };
  g.body = "static const int __zeno_begin = ::zeno::cell_mark(0);\n";
  for (auto& src : cells) {
    std::string code = src.code, uid = jq(src.uid);
    if (code.rfind("//%", 0) == 0) code.erase(0, 2);
    auto at = [&](int line) { return "\n#line " + std::to_string(src.line + line - 1) + " " + jq(src.file) + "\n"; };
    auto stmt = [&](const std::string& c, int line) { g.body += at(line) + "static const int " + var("__zeno_s") + " = ::zeno::run([] {" + at(line) + c + "\n});"; };
    bool synthetic = src.uid.ends_with(":args");   // main's argc/argv defaults: not a visible cell
    std::vector<std::string> cell_values;
    auto result = [&](const std::string& name) { if (synthetic) return; cell_values.push_back(name); g.body += "\nstatic const int " + var("__zeno_r") + " = ::zeno::result(" + uid + ", " + name + ");"; };
    auto reg = [&](const std::string& name) { g.body += "\nstatic const int " + var("__zeno_g") + " = ::zeno::reg(\"" + name + "\", " + name + ", " + sid + ");"; };
    // each cell nests a namespace, so a later cell's declaration hides an earlier one even within a run
    // Must nest exactly like the copy in `header`, so this cell and later ones name the same entities.
    std::string ns = "__c" + sid + "_" + std::to_string(&src - cells.data());
    size_t header_mark = g.header.size(), body_mark = g.body.size();
    g.header += "namespace " + ns + " {\n";
    g.body += "\nstatic const int " + var("__zeno_m") + " = ::zeno::cell_begin(" + uid + ");";
    for (auto& c : split(code)) {
      Kind k = classify(c);
      switch (k.k) {
        case 'p': {   // conditionals (and macros defined inside them) stay in place; the rest is hoisted to the prelude
          std::string d = word_at(c.text, c.text.find_first_not_of("# \t"));
          bool open = d == "if" || d == "ifdef" || d == "ifndef", close = d == "endif";
          if (open || close || d == "elif" || d == "else" || (cond > 0 && (d == "define" || d == "undef"))) {
            g.body += at(c.line) + c.text + "\n"; g.header += at(c.line) + c.text + "\n";
            cond += open - close;
          } else g.pre += at(c.line) + c.text + "\n";
          break;
        }
        case 'm': {
          std::string cmd = word_at(c.text, 1), arg = c.text.substr(std::min(c.text.size(), 1 + cmd.size()));
          if (cmd == "flags") g.flags += arg + " ";
          else if (cmd == "cflags") g.cflags += arg + " ";
          else if (cmd == "link") {   // sources are compiled into this run only; libraries stay linked for later runs
            std::istringstream in(arg);
            for (std::string w; in >> w;) {
              bool source = w.ends_with(".cpp") || w.ends_with(".cc") || w.ends_with(".cxx") || w.ends_with(".c");
              if (source && w[0] != '/') w = src.file.substr(0, src.file.rfind('/') + 1) + w;
              (source ? g.flags : g.ldflags) += w + " ";
            }
          }
          else if (cmd == "check") g.flags += " -fsanitize=undefined -D_GLIBCXX_ASSERTIONS -fno-omit-frame-pointer ";
          else if (cmd == "asm") g.want_asm = true;
          else if (cmd == "timeit") g.body += at(c.line) + "#error %timeit is now the TIMEIT(expr); macro\n";
          else g.body += at(c.line) + "#error unknown magic %" + cmd + " (try %flags %cflags %link %check %asm)\n";
          break;
        }
        case 'b': {   // auto [a, b] = e;  ->  inline auto __sb = e; auto& [a, b] = __sb;
          std::string text = c.text + (c.term ? "" : ";");
          size_t a = text.find('['), b = text.find(']', a);
          std::string sb = var("__zeno_sb"), names = text.substr(a + 1, b - a - 1);
          std::string decl = make_inline(strip_static(text.substr(0, a) + sb + text.substr(b + 1))) + "\nauto& [" + names + "] = " + sb + ";";
          g.body += at(c.line) + decl;
          g.header += at(c.line) + decl;
          std::istringstream in(names);
          for (std::string nm; std::getline(in, nm, ',');) { nm.erase(0, nm.find_first_not_of(" \t")); nm.erase(nm.find_last_not_of(" \t") + 1); reg(nm); }
          result(sb);
          break;
        }
        case 'q': g.body += at(c.line) + c.text + (c.term ? "" : ";"); break;   // reachable through the class
        case 'v': {
          std::string decl = make_inline(strip_static(c.text + (c.term ? "" : ";")));
          g.body += at(c.line) + decl;
          g.header += at(c.line) + decl;
          reg(k.name); result(k.name);
          break;
        }
        case 'f': {
          std::string text = c.text;
          g.body += at(c.line) + text;
          if (k.fn && text.back() == '}') text = make_inline(text);
          else if (text.compare(0, 9, "namespace") == 0) text = fix_namespace(text);
          g.header += at(c.line) + text;
          break;
        }
        default: {
          auto t = tokens(c.text, 1);
          if (!t.empty() && !kCtrl.count(t[0])) {   // expression statement: keep its value, hidden until revealed
            std::string e = c.text;
            while (!e.empty() && (e.back() == ';' || std::isspace((unsigned char)e.back()))) e.pop_back();
            std::string v = var("__zeno_v");
            // `static`, not `inline`: an inline variable nobody else uses may legally never be initialized
            g.body += at(c.line) + "static decltype(auto) " + v + " = ::zeno::wrap([]() -> decltype(auto) { return (" + at(c.line) + e + "\n); });";
            result(v);
          } else stmt(c.text, c.line);
        }
      }
      g.body += "\n";
    }
    if (g.header.size() == header_mark + ns.size() + 13) g.header.resize(header_mark);   // declared nothing
    else { g.ns.push_back(ns); g.body.insert(body_mark, "\nnamespace " + ns + " {"); }
    for (auto& v : cell_values) g.values.push_back({src.uid, v, g.ns.size()});
  }
  g.body += "static const int __zeno_end = ::zeno::cell_mark(1);\n" + std::string(g.ns.size(), '}') + "\n";
  return g;
}

// ---------------------------------------------------------------- source files as cells
// Code chunks become cells; comment blocks on their own lines become markdown; a markdown
// heading opens a collapsible section. A comment that looks like code stays code-looking ("ccode").
std::vector<Item> scan_source(const std::string& s) {
  std::vector<Item> items;
  auto line_of = [&](size_t i) { return 1 + (int)std::count(s.begin(), s.begin() + i, '\n'); };
  auto comments = [&](size_t a, size_t b) {
    std::string md; size_t mb = 0, me = 0; int ml = 0;
    auto flush = [&] {
      while (!md.empty() && md.back() == '\n') md.pop_back();
      if (md.empty()) return;
      int codey = 0, lines = 0;
      std::istringstream in(md);
      for (std::string l; std::getline(in, l);) {
        l.erase(l.find_last_not_of(" \t") + 1);
        if (l.empty()) continue;
        ++lines; char e = l.back();
        if (e == ';' || e == '{' || e == '}' || (e == ')' && l.find('(') != std::string::npos && l[0] != '-')) ++codey;
      }
      Item it{codey * 10 >= lines * 6 ? "ccode" : "md", md, mb, me, ml, 0};
      if (it.kind == "md" && md[0] == '#') it.level = (int)md.find_first_not_of('#');
      items.push_back(it); md.clear();
    };
    for (size_t p = a; p < b;) {
      size_t eol = std::min(s.find('\n', p), b);
      size_t f = s.find_first_not_of(" \t\r", p);
      if (f >= eol) { flush(); p = eol + 1; continue; }   // a blank line ends a block
      if (s.compare(f, 3, "//%") == 0) { flush(); items.push_back({"code", s.substr(f, eol - f), f, eol, line_of(f), 0}); p = eol + 1; continue; }
      if (md.empty()) { mb = f; ml = line_of(f); }
      if (s.compare(f, 2, "//") == 0) {
        size_t t = f + 2; while (t < eol && s[t] == '/') ++t;
        if (t < eol && s[t] == ' ') ++t;
        md += s.substr(t, eol - t) + "\n"; me = eol; p = eol + 1;
      } else if (s.compare(f, 2, "/*") == 0) {
        size_t e = std::min(s.find("*/", f), b);
        std::istringstream in(s.substr(f + 2, e - f - 2));
        for (std::string l; std::getline(in, l);) {
          size_t k = std::min(l.find_first_not_of(" \t"), l.size());
          if (k < l.size() && l[k] == '*') { ++k; if (k < l.size() && l[k] == ' ') ++k; }
          md += l.substr(k) + "\n";
        }
        me = std::min(b, e + 2);
        p = std::min(s.find('\n', me), b) + 1;
      } else p = eol + 1;
    }
    flush();
  };
  size_t pos = 0;
  for (auto& c : split(s)) {
    comments(pos, c.b);
    size_t e = c.e, k = e;   // a comment trailing on the same line belongs to the code
    while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
    if (s.compare(k, 2, "//") == 0) e = std::min(s.size(), s.find('\n', k));
    // int main(...) { ... }: its statements become cells, grouped under the signature
    static const std::regex main_re(R"(^\s*(?:int|auto|void)\s+main\s*\()");
    std::string text = s.substr(c.b, c.e - c.b);
    size_t open = std::string::npos;
    if (std::regex_search(text, main_re) && text.back() == '}') {
      int d = 0;
      for (size_t i = 0; i < text.size(); ++i) { if (text[i] == '(') ++d; else if (text[i] == ')') --d; else if (text[i] == '{' && d == 0) { open = i; break; } }
    }
    if (open != std::string::npos) {
      std::string sig = text.substr(0, open); sig.erase(sig.find_last_not_of(" \t\n") + 1);
      items.push_back({"fn", sig, c.b, c.b + open + 1, line_of(c.b), 0});
      size_t body_b = c.b + open + 1, body_e = c.e - 1;
      std::string pad(body_b, ' ');   // scan the body in place so offsets and lines stay file-relative
      for (size_t i = 0; i < body_b; ++i) if (s[i] == '\n') pad[i] = '\n';
      for (auto& it : scan_source(pad + s.substr(body_b, body_e - body_b))) items.push_back(it);
      items.push_back({"endfn", "}", body_e, c.e, line_of(body_e), 0});
      pos = e;
      continue;
    }
    items.push_back({"code", s.substr(c.b, e - c.b), c.b, e, line_of(c.b), 0});
    pos = e;
  }
  comments(pos, s.size());
  return items;
}

// ---------------------------------------------------------------- dependencies between cells
// Names a cell defines and identifiers it mentions. Statements, expressions and variable initializers may
// modify anything they mention, so callers treat those mentions as writes too (conservative, never too few).
Deps deps_of(const std::string& code) {
  Deps d;
  static const std::set<std::string> kw = {"auto", "const", "constexpr", "static", "inline", "int", "double", "float", "char", "bool",
                                           "long", "short", "unsigned", "signed", "void", "return", "if", "else", "for", "while", "do",
                                           "struct", "class", "union", "enum", "namespace", "template", "typename", "using", "typedef",
                                           "public", "private", "protected", "virtual", "override", "true", "false", "nullptr", "this",
                                           "new", "delete", "sizeof", "decltype", "operator", "std", "zeno", "break", "continue", "switch",
                                           "case", "default", "try", "catch", "throw", "noexcept", "explicit", "friend", "mutable", "volatile"};
  for (auto& c : split(code)) {
    Kind k = classify(c);
    auto t = tokens(c.text, 100000);
    if (k.k == 'f') {
      d.decl_only = d.decl_only && true;
      for (size_t i = 0; i + 1 < t.size(); ++i)   // struct/class/enum/namespace/concept/using NAME
        if ((t[i] == "struct" || t[i] == "class" || t[i] == "union" || t[i] == "enum" || t[i] == "namespace" || t[i] == "concept" || t[i] == "using") &&
            idstart(t[i + 1][0]) && !kw.count(t[i + 1])) { d.defs.insert(t[i + 1]); break; }
      if (!k.name.empty()) d.defs.insert(k.name.substr(k.name.rfind(':') == std::string::npos ? 0 : k.name.rfind(':') + 1));
    } else if (k.k == 'v' || k.k == 'b' || k.k == 'q') {
      d.decl_only = false;
      if (!k.name.empty()) d.defs.insert(k.name);
      if (k.k == 'b') { size_t a = c.text.find('['), b = c.text.find(']', a); for (auto& n : tokens(c.text.substr(a + 1, b - a - 1))) if (idstart(n[0])) d.defs.insert(n); }
    } else if (k.k != 'p') d.decl_only = false;
    if (k.k == 'p' && t.size() >= 2 && t[0] == "define") d.defs.insert(t[1]);
    for (auto& w : t) if (idstart(w[0]) && !kw.count(w)) d.uses.insert(w);
  }
  for (auto& n : d.defs) d.uses.erase(n);
  return d;
}
