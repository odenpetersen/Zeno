// Language-model client for chat and completion. Speaks the OpenAI chat-completions protocol, which
// llama.cpp, Ollama, vLLM, LM Studio, OpenRouter and OpenAI all serve; curl does the HTTP(S).
#include "common.hpp"
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace {
std::string config_path() {
  const char* x = getenv("XDG_CONFIG_HOME");
  return (x ? std::string(x) : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config") + "/zeno/config.json";
}
std::string trim_fences(std::string s) {   // models like to wrap answers in ``` even when told not to
  if (s.rfind("```", 0) == 0) { size_t nl = s.find('\n'); s = nl == std::string::npos ? "" : s.substr(nl + 1); }
  size_t e = s.rfind("```"); if (e != std::string::npos) s = s.substr(0, e);
  return s;
}
}  // namespace

Model model_config() {
  J j = J::parse(read_file(config_path()).empty() ? "{}" : read_file(config_path()));
  Model m{j["url"].str(), j["model"].str(), j["key"].str()};
  if (getenv("ZENO_MODEL_URL")) m.url = getenv("ZENO_MODEL_URL");
  if (getenv("ZENO_MODEL")) m.model = getenv("ZENO_MODEL");
  if (getenv("ZENO_API_KEY")) m.key = getenv("ZENO_API_KEY");
  while (!m.url.empty() && m.url.back() == '/') m.url.pop_back();
  return m;
}
void save_model_config(const Model& m) {
  std::filesystem::create_directories(std::filesystem::path(config_path()).parent_path());
  write_file(config_path(), "{\"url\":" + jq(m.url) + ",\"model\":" + jq(m.model) + ",\"key\":" + jq(m.key) + "}\n");
}

// Streams the assistant's reply through `on` and returns it whole. Errors come back as text starting with "[error]".
std::string chat(const std::string& messages_json, const std::function<void(const std::string&)>& on, int max_tokens, bool think,
                 const std::function<void()>& thinking) {
  Model m = model_config();
  if (m.url.empty()) { std::string e = "[error] no model configured: set one in the assistant's settings, or ZENO_MODEL_URL (an OpenAI-compatible base URL such as http://host:8080/v1)"; on(e); return e; }
  std::string body = "{\"model\":" + jq(m.model.empty() ? "default" : m.model) + ",\"messages\":" + messages_json +
                     ",\"stream\":true,\"temperature\":0.2" + (max_tokens > 0 ? ",\"max_tokens\":" + std::to_string(max_tokens) : "") +
                     // llama.cpp/vLLM/Ollama-style switch for thinking models; plain OpenAI rejects it, so we retry without
                     (think ? "" : ",\"chat_template_kwargs\":{\"enable_thinking\":false}") + "}";
  std::vector<std::string> argv = {"curl", "-sSN", "--max-time", "600", "-X", "POST", m.url + "/chat/completions", "-H", "Content-Type: application/json", "--data-binary", "@-"};
  if (!m.key.empty()) { argv.push_back("-H"); argv.push_back("Authorization: Bearer " + m.key); }
  std::string buf, all, raw;
  int rc = spawn_stream(argv, body, [&](const std::string& chunk) {
    raw += chunk.substr(0, 2000 - std::min<size_t>(raw.size(), 2000));
    buf += chunk;
    for (size_t nl; (nl = buf.find('\n')) != std::string::npos;) {
      std::string line = buf.substr(0, nl); buf.erase(0, nl + 1);
      if (line.rfind("data:", 0) != 0) continue;
      std::string d = line.substr(5); d.erase(0, d.find_first_not_of(' '));
      if (d == "[DONE]") continue;
      J dj = J::parse(d)["choices"][0]["delta"];
      std::string delta = dj["content"].str();
      if (!delta.empty()) { all += delta; on(delta); }
      else if (!dj["reasoning_content"].str().empty() && all.empty() && thinking) thinking();
    }
  });
  if (all.empty() && !think && raw.find("chat_template_kwargs") != std::string::npos)
    return chat(messages_json, on, max_tokens, true, thinking);
  if (all.empty()) {
    std::string e = "[error] the model at " + m.url + " returned nothing" + (rc ? " (curl exit " + std::to_string(rc) + ")" : "") + (raw.empty() ? "" : ": " + raw.substr(0, 400));
    on(e); return e;
  }
  return all;
}

// Fill-in-the-middle via chat: returns up to three single-line continuations for the cursor position.
std::vector<std::string> complete(const std::string& context, const std::string& prefix, const std::string& suffix) {
  std::string sys = "You are a C++ code completion engine inside a notebook. Reply with ONLY the text to insert at <CURSOR> "
                    "(usually the rest of the current line, at most a few lines). No explanation, no code fences, do not repeat text before the cursor.";
  std::string user = (context.empty() ? "" : "Notebook context (earlier cells):\n" + context.substr(context.size() > 6000 ? context.size() - 6000 : 0) + "\n\n") +
                     "Complete this cell:\n" + prefix + "<CURSOR>" + suffix;
  std::string msgs = "[{\"role\":\"system\",\"content\":" + jq(sys) + "},{\"role\":\"user\",\"content\":" + jq(user) + "}]";
  std::string out = trim_fences(chat(msgs, [](const std::string&) {}, 96, false));
  std::vector<std::string> items;
  if (out.rfind("[error]", 0) == 0) return items;
  // Models often repeat the text just before the cursor ("dot(" + "(a, a)"); drop such an overlap.
  for (size_t k = std::min(out.size(), prefix.size()); k > 0; --k)
    if (prefix.compare(prefix.size() - k, k, out, 0, k) == 0 && (k >= 3 || std::ispunct((unsigned char)out[0]))) { out.erase(0, k); break; }
  std::istringstream in(out);
  std::string first; std::getline(in, first);
  if (!first.empty()) items.push_back(first);
  std::string rest, l; for (int i = 0; i < 3 && std::getline(in, l); ++i) rest += "\n" + l;
  if (!rest.empty() && !first.empty()) items.push_back(first + rest);
  return items;
}
