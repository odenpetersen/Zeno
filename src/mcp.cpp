// `zeno mcp [path]`: a Model Context Protocol server (JSON-RPC over stdio) so coding agents can read, edit
// and run the notebook. It drives the project's `zeno web` server (starting one if needed), so a person
// watching in the browser sees the agent's runs live.
#include "common.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
int g_port = 0;
std::string g_token;

std::string http(const std::string& path, const std::string& body) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(g_port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(s, (sockaddr*)&a, sizeof a)) { close(s); return "{\"error\":\"zeno web server unreachable\"}"; }
  std::string req = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Token: " + g_token + "\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  for (size_t w = 0; w < req.size();) { ssize_t k = send(s, req.data() + w, req.size() - w, MSG_NOSIGNAL); if (k <= 0) break; w += k; }
  std::string resp; char buf[65536];
  for (ssize_t k; (k = read(s, buf, sizeof buf)) > 0;) resp.append(buf, k);
  close(s);
  size_t he = resp.find("\r\n\r\n");
  if (he == std::string::npos) return "";
  std::string head = resp.substr(0, he), b = resp.substr(he + 4);
  if (head.find("chunked") == std::string::npos) return b;
  std::string out;   // de-chunk
  for (size_t p = 0;;) {
    size_t nl = b.find("\r\n", p); if (nl == std::string::npos) break;
    size_t n = std::stoul(b.substr(p, nl - p), nullptr, 16); if (!n) break;
    out += b.substr(nl + 2, n); p = nl + 2 + n + 2;
  }
  return out;
}

// Find the web server for this project, or start one in the background.
bool connect_server(const std::string& root) {
  std::string want = fs::absolute(root).lexically_normal().string(), dir = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.cache/zeno/servers";
  auto scan = [&] {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
      J j = J::parse(read_file(e.path().string()));
      if (j["root"].str() == want && kill((pid_t)j["pid"].num(), 0) == 0) { g_port = (int)j["port"].num(); g_token = j["token"].str(); return true; }
    }
    return false;
  };
  if (scan()) return true;
  if (fork() == 0) {
    setsid();
    int n = open("/dev/null", O_RDWR); dup2(n, 0); dup2(n, 1); dup2(n, 2);
    execl("/proc/self/exe", "zeno", "web", want.c_str(), "--no-browser", "--port", "8890", (char*)nullptr);
    _exit(127);
  }
  for (int i = 0; i < 100; ++i) { usleep(100000); if (scan()) return true; }
  return false;
}

std::string str_field(const J& a, const char* k) { return a[k].str(); }
std::string body(std::initializer_list<std::pair<const char*, std::string>> kv) {   // values are already JSON
  std::string o = "{";
  for (auto& [k, v] : kv) o += (o.size() > 1 ? "," : "") + jq(k) + ":" + v;
  return o + "}";
}
std::string clip(std::string s, size_t n = 30000) { return s.size() > n ? s.substr(0, n) + "\n… (truncated)" : s; }

std::string run(const J& a) {
  std::string events;
  if (a.has("group") || a["force"].b)   // explicit: run exactly these cells, whatever their state
    events = http("/api/run", body({{"target", jq(a.has("uid") ? str_field(a, "uid") : str_field(a, "group"))}, {"kind", jq(a.has("uid") ? "cell" : "group")}, {"origin", jq("mcp")}}));
  else events = http("/api/sync", body({{"upto", jq(str_field(a, "uid"))}, {"origin", jq("mcp")}}));
  if (events.find("\"uptodate\"") != std::string::npos) return "already up to date: nothing needed re-running (use results to read outputs, or force:true to re-run anyway)";
  std::vector<std::string> uids; std::string tail;
  std::istringstream in(events);
  for (std::string l; std::getline(in, l);) {
    J e = J::parse(l);
    if (e["ev"].str() == "batch") for (auto& u : e["uids"].a) uids.push_back(u.str());
    if (e["ev"].str() == "done") tail = e["ok"].b ? "run succeeded" : "run FAILED and was rolled back: " + e["why"].str("compile error");
  }
  if (uids.empty()) return "nothing to run (unknown uid or group?)";
  std::string o;
  for (auto& u : uids) {
    std::string r = J::parse(http("/api/results", body({{"uid", jq(u)}})))["text"].str();
    if (r != "(ran; no output)" || uids.size() == 1) o += "[" + u + "]\n" + r + (r.empty() || r.back() == '\n' ? "" : "\n");
  }
  return clip(o + tail);
}
std::string op(const std::string& name, const std::string& fields = "") {
  std::string ev = http("/api/op", "{\"op\":" + jq(name) + (fields.empty() ? "" : "," + fields) + "}"), o;
  std::istringstream in(ev);
  for (std::string l; std::getline(in, l);) {
    J e = J::parse(l); std::string k = e["ev"].str();
    if (k == "done") { if (e.has("why")) o += e["why"].str() + "\n"; continue; }
    if (k == "layout" || k == "source") o += e["text"].str();
    else if (k == "macro") o += e["m"]["text"].str();
    else if (k != "status") o += l + "\n";
  }
  return clip(o.empty() ? "ok" : o);
}

struct Tool { const char* name; const char* desc; const char* schema; std::function<std::string(const J&)> fn; };
const char* kUid = R"j({"type":"object","properties":{"uid":{"type":"string","description":"cell uid like main.cpp:3 (from outline)"}},"required":["uid"]})j";
std::vector<Tool> tools() {
  return {
    {"outline", "The notebook's structure: files, #include groups, markdown headings and every cell with its uid and first line.", R"j({"type":"object","properties":{}})j",
     [](const J&) { return J::parse(http("/api/outline", "{}"))["text"].str(); }},
    {"list_files", "List files under the project folder (or a subfolder).", R"j({"type":"object","properties":{"path":{"type":"string"}}})j",
     [](const J& a) { return J::parse(http("/api/files", body({{"action", "\"list\""}, {"path", jq(a["path"].str())}})))["text"].str(); }},
    {"read_file", "Read a file in the project folder.", R"j({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})j",
     [](const J& a) { return clip(J::parse(http("/api/files", body({{"action", "\"read\""}, {"path", jq(a["path"].str())}})))["text"].str(), 100000); }},
    {"write_file", "Create or overwrite a file in the project folder (e.g. a new header to #include).", R"j({"type":"object","properties":{"path":{"type":"string"},"text":{"type":"string"}},"required":["path","text"]})j",
     [](const J& a) { return http("/api/files", body({{"action", "\"write\""}, {"path", jq(a["path"].str())}, {"text", jq(a["text"].str())}})); }},
    {"read_cell", "A cell's source and the output of its last run.", kUid,
     [](const J& a) {
       J c = J::parse(http("/api/cellsrc", body({{"uid", jq(a["uid"].str())}})));
       if (!c.has("code")) return std::string("no such cell");
       return c["file"].str() + ":" + std::to_string((int)c["line"].num()) + "\n```cpp\n" + c["code"].str() + "\n```\nlast run:\n" +
              J::parse(http("/api/results", body({{"uid", jq(a["uid"].str())}})))["text"].str();
     }},
    {"edit_cell", "Replace a cell's source in its file.", R"j({"type":"object","properties":{"uid":{"type":"string"},"text":{"type":"string"}},"required":["uid","text"]})j",
     [](const J& a) { return http("/api/cell", body({{"action", "\"replace\""}, {"uid", jq(a["uid"].str())}, {"text", jq(a["text"].str())}})); }},
    {"insert_cell", "Insert a new cell after the given one.", R"j({"type":"object","properties":{"after":{"type":"string"},"text":{"type":"string"}},"required":["after","text"]})j",
     [](const J& a) { return http("/api/cell", body({{"action", "\"insert_after\""}, {"uid", jq(a["after"].str())}, {"text", jq(a["text"].str())}})); }},
    {"delete_cell", "Delete a cell from its file.", kUid,
     [](const J& a) { return http("/api/cell", body({{"action", "\"delete\""}, {"uid", jq(a["uid"].str())}})); }},
    {"run", "Bring the notebook up to date (Sync): runs exactly the cells that changed, are new, or depend on a change, rolling back first when needed, so state always equals a top-to-bottom run. With uid: up to that cell. With group (file path or file@N section) or force:true: run those cells unconditionally. Returns each cell's output, values (type + reveal ref), compiler errors, crashes. A failed run is rolled back.",
     R"j({"type":"object","properties":{"uid":{"type":"string"},"group":{"type":"string"},"force":{"type":"boolean"}}})j", run},
    {"results", "Output of a cell's last run.", kUid, [](const J& a) { return J::parse(http("/api/results", body({{"uid", jq(a["uid"].str())}})))["text"].str(); }},
    {"reveal", "Inspect a value: pass a reveal ref from run/results output (or from a pointer field). Returns type, size, layout with padding, fields/elements, and refs to dereference pointers.",
     R"j({"type":"object","properties":{"ref":{"type":"string"}},"required":["ref"]})j", [](const J& a) { return op("expand", "\"ref\":" + jq(a["ref"].str())); }},
    {"variables", "Every variable currently defined, with type and value summary.", R"j({"type":"object","properties":{}})j", [](const J&) { return op("vars"); }},
    {"heap", "Live heap memory by allocation site and by run.", R"j({"type":"object","properties":{}})j", [](const J&) { return op("heap"); }},
    {"leaks", "Heap blocks no longer reachable from any variable (valgrind-style).", R"j({"type":"object","properties":{}})j", [](const J&) { return op("leaks"); }},
    {"disassemble", "Machine code generated for the run that last executed a cell.", kUid,
     [](const J& a) { int b = (int)J::parse(http("/api/results", body({{"uid", jq(a["uid"].str())}})))["batch"].num(); return b ? op("asm", "\"id\":" + std::to_string(b)) : std::string("run the cell first"); }},
    {"layout", "Memory layout of a type (offsets, sizes, holes), from debug info.", R"j({"type":"object","properties":{"type":{"type":"string"}},"required":["type"]})j",
     [](const J& a) { return op("layout", "\"type\":" + jq(a["type"].str())); }},
    {"undo", "Roll back the most recent run (state returns to before it).", R"j({"type":"object","properties":{}})j", [](const J&) { return op("undo"); }},
  };
}

void reply(const J& id, const std::string& result) { std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id.dump() << ",\"result\":" << result << "}\n" << std::flush; }
void error(const J& id, int code, const std::string& msg) { std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id.dump() << ",\"error\":{\"code\":" << code << ",\"message\":" << jq(msg) << "}}\n" << std::flush; }
}  // namespace

int mcp_main(const std::string& root) {
  signal(SIGPIPE, SIG_IGN);
  bool up = connect_server(root.empty() ? "." : root);
  auto ts = tools();
  for (std::string line; std::getline(std::cin, line);) {
    if (line.find_first_not_of(" \r\t") == std::string::npos) continue;
    J m = J::parse(line);
    std::string method = m["method"].str();
    const J& id = m["id"];
    if (id.t == J::Null) continue;   // notifications
    if (method == "initialize")
      reply(id, "{\"protocolVersion\":" + jq(m["params"]["protocolVersion"].str("2025-06-18")) +
                ",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"zeno\",\"version\":\"0.1\"},"
                "\"instructions\":\"Zeno is a C++ notebook: plain C++ files where each top-level statement is a cell. Start with `outline`; "
                "edit cells by uid; `run` returns outputs and value refs you can `reveal`.\"}");
    else if (method == "ping") reply(id, "{}");
    else if (method == "tools/list") {
      std::string o;
      for (auto& t : ts) o += (o.empty() ? "" : ",") + std::string("{\"name\":") + jq(t.name) + ",\"description\":" + jq(t.desc) + ",\"inputSchema\":" + t.schema + "}";
      reply(id, "{\"tools\":[" + o + "]}");
    } else if (method == "tools/call") {
      std::string name = m["params"]["name"].str(), out; bool found = false;
      for (auto& t : ts) if (name == t.name) { found = true; out = up ? t.fn(m["params"]["arguments"]) : "could not start or find an zeno web server for this project"; }
      if (!found) { error(id, -32602, "unknown tool " + name); continue; }
      bool err = out.rfind("{\"error\"", 0) == 0 || out.rfind("{\"ok\":false", 0) == 0;
      reply(id, "{\"content\":[{\"type\":\"text\",\"text\":" + jq(out) + "}],\"isError\":" + (err ? "true" : "false") + "}");
    } else error(id, -32601, "method not found: " + method);
  }
  return 0;
}
