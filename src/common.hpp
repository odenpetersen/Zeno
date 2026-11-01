// Shared internals: a tiny JSON value, process helpers, and module interfaces.
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <set>

// ---------------------------------------------------------------- JSON
struct J {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false; double n = 0; std::string s;
  std::vector<J> a; std::vector<std::pair<std::string, J>> o;
  const J& operator[](std::string_view k) const;
  const J& operator[](std::size_t i) const { static J nil; return i < a.size() ? a[i] : nil; }
  std::string str(std::string_view def = "") const { return t == Str ? s : std::string(def); }
  double num(double def = 0) const { return t == Num ? n : t == Bool ? b : def; }
  bool has(std::string_view k) const { return (*this)[k].t != Null; }
  static J parse(std::string_view text);
  std::string dump() const;
};
std::string jq(std::string_view s);                   // quoted, escaped JSON string

// ---------------------------------------------------------------- helpers
struct Proc { int status = -1; std::string out; double ms = 0; };
Proc run_cmd(const std::vector<std::string>& argv, const std::string& input = "");  // captures stdout+stderr
int spawn_stream(const std::vector<std::string>& argv, const std::string& input, const std::function<void(const std::string&)>& on);  // stdout as it arrives
std::string read_file(const std::string& path);
bool write_file(const std::string& path, std::string_view data);
std::string self_dir();                                // directory of the zeno executable
std::string strip_cell_ns(std::string s);              // "__c1::__c4::P" -> "P"

// ---------------------------------------------------------------- cell -> translation unit
struct Gen {
  std::string pre, body, header;   // preprocessor lines, this cell's code, copy for later cells
  std::string flags, cflags, ldflags;
  bool want_asm = false;
  std::vector<std::string> ns;     // namespaces `header` opens (one per cell that declares something)
  struct Value { std::string uid, name; size_t ns_count; };   // a cell's value: `name` inside the first ns_count of `ns`
  std::vector<Value> values;
};
struct Src { std::string uid, code, file; int line = 1; };   // one cell to execute
Gen gen_batch(const std::vector<Src>& cells, int id);
struct Deps { std::set<std::string> defs, uses; bool decl_only = true; };   // decl_only: types/functions only, no runtime effects
Deps deps_of(const std::string& code);

// ---------------------------------------------------------------- projects: folders of plain C++ files
struct Item { std::string kind, text; size_t b = 0, e = 0; int line = 0, level = 0; };   // kind: code | md | ccode | fn | endfn
std::vector<Item> scan_source(const std::string& text);
std::string project_json(const std::string& root);      // display tree (dirs, files, sections, cells)
size_t project_signature(const std::string& root);      // changes whenever any source file changes
bool find_cell(const std::string& root, const std::string& uid, Src& out, Item* item = nullptr);
std::vector<Src> cells_under(const std::string& root, const std::string& prefix);   // every cell in a dir/file/section
bool is_readonly(const std::string& root, const std::string& file);   // .zeno.json {"readonly": ["vendor/**", ...]}
bool replace_cell(const std::string& root, const std::string& uid, const std::string& text, bool insert_after = false);
bool replace_region(const std::string& root, const std::string& file, size_t b, size_t e, const std::string& orig, const std::string& text,
                    bool before = false, bool raw = false, std::string* removed = nullptr);
std::string read_region(const std::string& root, const std::string& file, size_t b, size_t e);

// ---------------------------------------------------------------- language model (any OpenAI-compatible server)
struct Model { std::string url, model, key; };   // url: base such as http://host:8080/v1
Model model_config();                             // ~/.config/zeno/config.json, overridden by ZENO_MODEL_URL / ZENO_MODEL / ZENO_API_KEY
void save_model_config(const Model& m);
// Streams the reply's text through `on`. think=false asks thinking models to answer directly; `thinking` fires while they reason.
std::string chat(const std::string& messages_json, const std::function<void(const std::string&)>& on, int max_tokens = 0,
                 bool think = true, const std::function<void()>& thinking = {});
std::vector<std::string> complete(const std::string& context, const std::string& prefix, const std::string& suffix);
std::string outline_text(const std::string& root, size_t limit = 12000);   // compact outline with cell uids, for prompts and agents

// ---------------------------------------------------------------- kernel client (frontends)
struct Kernel {
  int pid = -1, req = -1, out = -1, err = -1, running = -1;
  std::vector<int> chain;          // pids of state snapshots, oldest first
  int next_id = 1, max_snapshots = 24;
  std::string mode;                // "web" | "tui"
  void start();
  void stop();
  // Send a request; callbacks receive stdout text, stderr text and events until the "done" event (returned).
  J call(const std::string& op, const std::string& fields,
         const std::function<void(const std::string& stream, const std::string& data)>& on);
  void interrupt();
};
int kernel_main();
int tui_main(const std::string& path);
int run_main(const std::string& notebook);
int web_main(const std::string& notebook, int port, bool open_browser, const std::string& host = "127.0.0.1", bool token = false);
int mcp_main(const std::string& root);
int new_main(const std::string& dir);
int doctor_main();
extern const char* const kStarter;   // the starter notebook `zeno new` writes
