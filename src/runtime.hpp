// Kernel <-> runtime interface (both live in the zeno binary).
#pragma once
#include <string>
#include <vector>
#include "../include/zeno.hpp"

namespace zeno { std::string demangle(const char* mangled); }
namespace heap {
struct Stats { long allocs = 0, frees = 0; size_t bytes = 0, live = 0, peak = 0, blocks = 0; };
void enable(bool);
}
namespace rt {
extern bool web;                       // frontend renders SVG (else terminal text)
using Perf_ = void*;
Perf_ perf_begin();
zeno::counters perf_end(Perf_);
heap::Stats heap_stats();
void heap_cell_begin(int cell);
std::string vars_json();
struct Mark { bool begun = false, ended = false; heap::Stats h0, h1; zeno::counters c; double t0 = 0, t1 = 0; long ut0 = 0, ut1 = 0; };
extern Mark mark;
double now_ms();
std::string heap_json();
std::string leaks_json();
std::string gdb_bt(int pid, const char* after);
std::vector<std::string> resolve(const std::vector<const void*>& sites);
}
