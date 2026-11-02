// Onboarding: `zeno new` (a starter notebook) and `zeno doctor` (toolchain check + cache warm-up).
#include "common.hpp"
#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

const char* const kStarter = R"cpp(// # Welcome to Zeno
//
// This file is a notebook. Every top-level statement is a cell, and comments
// like this one are markdown. Press **⟳ Sync** to run, click **reveal** under a
// cell to see its value, and click a cell to edit it.

#include <vector>

// ## Values you can look inside

struct Sample { char tag; double value; int count; };

Sample s{'a', 3.14, 7};

std::vector<double> xs = zeno::linspace(0.0, 6.28, 200);
std::vector<double> ys;

for (double x : xs) ys.push_back(std::sin(x) + 0.1 * std::sin(17 * x));

// ## Plots and timing

zeno::plot(xs, ys).title("a wobbly sine");

TIMEIT(std::accumulate(ys.begin(), ys.end(), 0.0));

// ## Change something
//
// Edit `s` above, press Sync, and only the cells that depend on it re-run.
)cpp";

int new_main(const std::string& dir) {
  fs::path d = dir.empty() ? "." : dir;
  fs::create_directories(d);
  fs::path f = d / "main.cpp";
  if (fs::exists(f)) { std::printf("%s already exists; not touching it.\n", f.c_str()); return 1; }
  write_file(f.string(), kStarter);
  write_file((d / ".gitignore").string(), "build/\n");
  std::printf("Created %s\n\nNext:\n  zeno web %s            # open it in the browser\n  zeno %s                # or in the terminal\n",
              f.c_str(), d.c_str(), d.c_str());
  return 0;
}

namespace {
int fails = 0;
void line(bool ok, const std::string& what, const std::string& hint = "", bool required = true) {
  std::printf("  %s %s\n", ok ? "\x1b[32m✓\x1b[0m" : required ? "\x1b[31m✗\x1b[0m" : "\x1b[33m!\x1b[0m", what.c_str());
  if (!ok && !hint.empty()) std::printf("      %s\n", hint.c_str());
  if (!ok && required) ++fails;
}
bool has(const char* tool) { return run_cmd({"sh", "-c", std::string("command -v ") + tool}).status == 0; }
std::string first_line(std::string s) { return s.substr(0, s.find('\n')); }
int read_int(const char* path) { std::string s = read_file(path); return s.empty() ? -1 : std::atoi(s.c_str()); }
}  // namespace

int doctor_main() {
  std::printf("Zeno doctor\n\nToolchain\n");
  std::string cxx = getenv("ZENO_CXX") ? getenv("ZENO_CXX") : "g++";
  Proc v = run_cmd({cxx, "-dumpfullversion"});
  int major = v.status == 0 ? std::atoi(v.out.c_str()) : 0;
  line(major >= 11, cxx + " " + (v.status == 0 ? first_line(v.out) : "not found") + " (need 11+)", "install g++ 11 or newer, e.g. sudo apt install g++");
  line(has("gdb"), "gdb: crash backtraces, memory layouts, stack snapshots", "sudo apt install gdb", false);
  line(has("objdump") && has("addr2line"), "binutils: disassembly and heap allocation sites", "sudo apt install binutils", false);
  line(has("vim"), "vim: the default editor behind `e` (ZENO_EDITOR or $EDITOR picks another)", "sudo apt install vim", false);
  line(has("git"), "git: change markers", "", false);
  std::printf("\nSystem\n");
  int ptrace = read_int("/proc/sys/kernel/yama/ptrace_scope");
  line(ptrace <= 1, "ptrace allowed (scope " + std::to_string(ptrace) + "), so gdb can attach for backtraces", "set /proc/sys/kernel/yama/ptrace_scope to 1", false);
  int paranoid = read_int("/proc/sys/kernel/perf_event_paranoid");
  line(paranoid <= 2, "hardware counters available (perf_event_paranoid " + std::to_string(paranoid) + ")", "sudo sysctl kernel.perf_event_paranoid=2 for cycles/instructions", false);

  std::printf("\nFirst run (builds the precompiled header once, ~5 s)\n");
  Kernel k; k.mode = "tui"; k.start();
  std::string diag, err;
  J r = k.call("exec", "\"id\":1,\"cells\":[{\"uid\":\"doctor\",\"code\":\"std::vector<int> v{1, 2, 3};\",\"file\":\"\",\"line\":1}]",
               [&](const std::string& s, const std::string& d) {
                 if (s != "ev") return;
                 J e = J::parse(d);
                 if (e["ev"].str() == "compile" && !e["ok"].b) diag = e["diag"].str();
                 if (e["ev"].str() == "error" || e["ev"].str() == "crash") err = e["what"].str(e["sig"].str());
               });
  k.stop();
  line(r["ok"].b, "compiled and ran a cell", diag.empty() ? err : first_line(diag));

  std::printf("\nAssistant (optional)\n");
  Model m = model_config();
  if (m.url.empty()) line(false, "no model configured", "set one in the browser's Assistant → settings, or ZENO_MODEL_URL=http://host:port/v1", false);
  else {
    Proc c = run_cmd({"curl", "-s", "-m", "5", "-o", "/dev/null", "-w", "%{http_code}", m.url + "/models"});
    line(c.out == "200", "model server " + m.url + " (" + (c.out.empty() ? "unreachable" : "HTTP " + c.out) + ")", "is the server running?", false);
  }
  std::printf("\n%s\n", fails ? "Fix the ✗ items above, then run `zeno doctor` again." : "Ready. Try:  zeno new hello && zeno web hello");
  return fails ? 1 : 0;
}
