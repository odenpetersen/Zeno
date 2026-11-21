// Reference pages for the std names a cell uses, from cppreference (its name index, then the page's content),
// cached in ~/.cache/zeno/cppref.
#include "common.hpp"
#include <filesystem>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <tuple>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <unistd.h>

namespace {
const char* kIndexUrl = "https://raw.githubusercontent.com/PeterFeicht/cppreference-doc/master/index-functions-cpp.xml";
const char* kSite = "https://en.cppreference.com/w/";

std::string cache_dir() {
  const char* xdg = getenv("XDG_CACHE_HOME");
  std::string d = (xdg ? std::string(xdg) : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.cache") + "/zeno/cppref";
  std::error_code ec; std::filesystem::create_directories(d, ec);
  return d;
}

bool fetch(const std::string& url, const std::string& to) {
  std::string tmp = to + ".part";
  Proc p = run_cmd({"curl", "-sfL", "-m", "20", "-o", tmp, url});
  if (p.status != 0) { unlink(tmp.c_str()); return false; }
  return rename(tmp.c_str(), to.c_str()) == 0;
}

// name -> page link (e.g. "std::priority_queue" -> "cpp/container/priority_queue"), with class members ("std::vector::push_back")
std::map<std::string, std::string> g_index;
std::mutex g_mu;
bool load_index() {
  std::lock_guard<std::mutex> l(g_mu);
  if (!g_index.empty()) return true;
  std::string f = cache_dir() + "/index.xml";
  if (read_file(f).empty() && !fetch(kIndexUrl, f)) return false;
  static const std::regex top("^    <\\w+ name=\"([^\"]+)\" link=\"([^\"]+)\""), member("^        <\\w+ name=\"(\\w+)\"(?: link=\"([^\"]+)\")?");
  std::istringstream in(read_file(f));
  std::smatch m; std::string cls, at;   // the enclosing class and its page
  for (std::string line; std::getline(in, line);)
    if (std::regex_search(line, m, top)) { g_index.emplace(cls = m[1].str(), at = m[2].str()); }
    else if (!cls.empty() && std::regex_search(line, m, member))   // a member's link is relative to its class page
      g_index.emplace(cls + "::" + m[1].str(), m[2].str() == "." ? at : at + "/" + (m[2].matched ? m[2].str() : m[1].str()));
  return !g_index.empty();
}

std::string names_json(const std::vector<std::string>& names) {
  std::string o = "{\"names\":[";
  for (size_t i = 0; i < names.size(); ++i) o += (i ? "," : "") + std::string("{\"name\":") + jq(names[i]) + ",\"link\":" + jq(g_index[names[i]]) + "}";
  return o + "]}";
}

bool safe_link(const std::string& l) { return l.rfind("cpp/", 0) == 0 && l.find("..") == l.npos && l.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/.-") == l.npos; }

}  // namespace

std::string ref_names_json(const std::string& code, bool using_std) {
  if (!load_index()) return "{\"error\":" + jq("couldn't download the cppreference index (" + std::string(kIndexUrl) + ")") + "}";
  auto t = code_tokens(code);
  std::vector<std::string> out;
  std::set<std::string> seen;
  auto add = [&](const std::string& n) { if (seen.insert(n).second) out.push_back(n); };
  for (size_t i = 0; i < t.size(); ++i) {
    bool member = i > 0 && (t[i - 1] == "." || t[i - 1] == "->" || t[i - 1] == "::");
    if (t[i] == "std" && !member) {   // the longest std::a::b... the index knows
      std::string n = "std"; std::string best;
      for (size_t j = i + 1; j + 1 < t.size() && t[j] == "::" && !t[j + 1].empty() && (std::isalpha((unsigned char)t[j + 1][0]) || t[j + 1][0] == '_'); j += 2) {
        n += "::" + t[j + 1];
        if (g_index.count(n)) best = n;
      }
      if (!best.empty()) add(best);
    } else if (using_std && !member && !t[i].empty() && (std::isalpha((unsigned char)t[i][0]) || t[i][0] == '_') && g_index.count("std::" + t[i])) {
      add("std::" + t[i]);
    }
  }
  return names_json(out);
}

// Names containing q (case-insensitive): exact names first, then those whose last part starts with q; shallow, short ones first.
std::string ref_search_json(std::string q) {
  if (!load_index()) return "{\"error\":" + jq("couldn't download the cppreference index (" + std::string(kIndexUrl) + ")") + "}";
  auto low = [](std::string x) { for (auto& c : x) c = std::tolower((unsigned char)c); return x; };
  q = low(q);
  std::vector<std::pair<std::tuple<int, size_t, std::string>, std::string>> hits;
  for (auto& [n, l] : g_index) {
    std::string ln = low(n), base = ln.substr(0, ln.find('(')), last = base.substr(base.rfind(':') + 1);
    if (ln.find(q) == ln.npos) continue;
    int tier = ln == q || ln == "std::" + q ? 0 : last.rfind(q, 0) == 0 ? 1 : base.find(q) != base.npos ? 2 : 3;
    hits.push_back({{tier, std::count(base.begin(), base.end(), ':') * 100 + n.size(), n}, n});
  }
  std::sort(hits.begin(), hits.end());
  std::vector<std::string> out;
  for (size_t i = 0; i < hits.size() && i < 40; ++i) out.push_back(hits[i].second);
  return names_json(out);
}

std::string ref_page_json(const std::string& what) {
  std::string link = what;
  if (what.rfind("std", 0) == 0) { load_index(); std::lock_guard<std::mutex> l(g_mu); auto it = g_index.find(what); link = it == g_index.end() ? "" : it->second; }
  if (!link.empty() && link.back() == '/') link.pop_back();
  if (link.size() > 5 && link.compare(link.size() - 5, 5, ".html") == 0) link.resize(link.size() - 5);
  std::string url = kSite + link;
  if (safe_link(link)) {
    std::string f = cache_dir() + "/" + std::regex_replace(link, std::regex("/"), "_") + ".html";
    if (!read_file(f).empty() || fetch(url, f)) {
      std::string page = read_file(f);
      size_t b = page.find("<div id=\"mw-content-text\">"), e = page.find("NewPP limit report");
      if (b != page.npos && e != page.npos && e > b) {
        e = page.rfind("<!--", e);
        std::string title;
        size_t h = page.find("<h1"), he = page.find("</h1>", h);
        if (h != page.npos && he != page.npos) title = std::regex_replace(page.substr(h, he - h), std::regex("<[^>]*>"), "");
        return "{\"title\":" + jq(title) + ",\"url\":" + jq(url) + ",\"html\":" + jq(page.substr(b, e - b)) + "}";
      }
    }
  }
  return "{\"error\":" + jq(safe_link(link) ? "couldn't load " + url + " (offline?)" : "no reference page for " + what) + "}";
}
