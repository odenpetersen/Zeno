# Zeno — a C++ notebook

A Jupyter-style notebook for C++, written in C++ (about 5,000 lines). A notebook is an ordinary
folder of `.cpp`/`.hpp` files. There is no special format, so git, editors and coding agents work
on it as they would on any other code.

## Getting started

On Linux with g++ 11 or newer:

```sh
git clone https://github.com/odenpetersen/Zeno && cd Zeno
./install.sh                 # builds, installs ~/.local/bin/zeno, then runs `zeno doctor`
zeno new hello && zeno web hello
```

`zeno web` prints a link; open it, press **⟳ Sync**, and click **reveal** under any cell.
`zeno doctor` checks the toolchain (gdb, binutils and vim are optional) and builds the
compiler cache, so your first cell runs quickly. It also tells you what to install if
something is missing. Opening an empty folder in the browser offers the same starter notebook.

| Command | What it does |
|---|---|
| `zeno new [dir]` | write a short tour notebook to `dir/main.cpp` |
| `zeno web [path] [--host 0.0.0.0] [--port N] [--token]` | browser notebook; `--host 0.0.0.0` makes it reachable from your network, `--token` requires a secret in the URL |
| `zeno [path]` | terminal notebook (`.help` lists commands) |
| `zeno run [path]` | run everything and print the outputs; non-zero exit on failure |
| `zeno mcp [path]` | MCP server so coding agents can read, edit and run the notebook |
| `zeno doctor` | check the setup |

To try the bundled examples without installing: `make && ./zeno web examples/demo`.

## The format is just C++

- **The entry point is the notebook.** `zeno web dir/` uses `dir/main.cpp`; a file path works too.
- **Every top-level statement is a cell**: declarations, functions, expression statements, a whole `if` or `for` block.
- **Comments on their own lines are markdown.** A heading (`// ## Results`) starts a collapsible section. Commented-out code is detected and shown as code.
- **Local `#include "x.hpp"` are collapsible groups** holding that file's cells, run where they are included. Include guards and `#pragma once` are hidden.
- **`int main(...) { … }` is a group of cells**, one per statement, so an ordinary program opens as a notebook. Its locals become inspectable variables, and `argc`/`argv` get defaults.
- **Magic comments:**
  - `//%flags -O3` (this cell)
  - `//%cflags …` (this and later cells)
  - `//%link -lfoo` or `//%link other.cpp`
  - `//%check` (UBSan and libstdc++ assertions)
  - `//%asm`
- **Read-only parts:** list globs in `.zeno.json` at the project root, e.g. `{"readonly": ["vendor/**", "third_party/**"]}`. Edits from the page and from agents are refused there.

## Running

Each cell's toolbar has a **run ▾** menu (also on right-click of ▶), with keys:

- **Bring up to date** (Shift+Enter, ▶): the least re-running that leaves the state as if this cell and everything it depends on had run in file order. It runs nothing if that's already so.
  - It re-runs cells that changed or are new, and cells that read what those write.
  - If a variable's history is wrong (a mutation was edited or deleted, or a later cell modified it), it is rebuilt from its declaration. Declaring it again starts it fresh, so nothing is rolled back.
- **Run just this** (`r`): the cell alone, whatever its state.
- **Re-run it and all it depends on** (`R`).
- **Run everything above it too** (`A`): every earlier cell in file order, then this one.
- **Run all** (header) restarts the kernel and runs every cell. **⟳ Sync** brings the whole notebook up to date.
- **Dependencies** are by name: a cell depends on earlier cells that write a name it reads, including what the functions it calls read. A statement is taken to modify a name when it assigns or increments it, passes it to a function, takes its address or calls a method on it (other than `size()` and similar read-only ones). Reading, printing with `<<` or reading a member is not modifying. When in doubt it assumes modified, so it re-runs too much rather than too little.
- **Every run is a transaction.** Each run executes in a `fork()` of the previous state. A crash, exception or Ctrl-C/Interrupt discards the child, and the state is exactly as before. ⟲ Undo steps back one run; nothing else rolls back.
- **Redeclaration works:** every cell gets a nested namespace, so the newest declaration of a name wins.

## Seeing values

- **Results are hidden until revealed.** Each value-producing cell shows its type with a *reveal* link. Before a cell has ever run, the compiler supplies its type and memory layout ("not run · layout").
- **Structs** show a memory map: one row per 8 bytes, each field a coloured block spanning its bytes, padding hatched, and a note when reordering the fields would shrink the struct. A table below lists offset, size, type and value; hovering a block highlights its row. Before a cell runs, the same map comes from the compiler.
- **Containers:**
  - Numeric sequences get a sparkline and min/max/mean/std.
  - Vectors of structs become tables.
  - Maps are keyed rows.
- **Pointers** dereference on click (▸ \*). A pointer into the heap says which block and run allocated it, and a heap block can be viewed as an array ([ ]). Classes with private members show their bytes, plus the layout from gdb.
- **Plots:**
  - Calls: `zeno::plot(v)`, `zeno::scatter(x, y)`, `zeno::hist(v, bins)`, `zeno::bar(labels, v)`, `zeno::heatmap(m)`, with `.title()`, `.xlabel()`, `.logy()`.
  - Output: SVG in the browser, braille characters in the terminal.
- **Data helpers:** `zeno::read_csv(path)` returns a table with `.num("col")` and `.col("col")`. `zeno::linspace`.
- **Displaying explicitly:** `zeno::display(x)` shows any value immediately.

## Performance and debugging

- **Per run:**
  - compile time and run time
  - instructions and IPC, cache misses and branch misses (hardware counters)
  - allocations and heap growth, peak stack, RSS
- **Per cell:** run time.
- **Benchmarks:** `TIMEIT(expr)` times `expr` like `%timeit` and evaluates to a `zeno::bench`: mean ± std per loop, the 7 runs, min, cycles, instructions and IPC. A cell shows it straight away as a card (a line in the terminal); elsewhere `std::cout << TIMEIT(f(x))` prints it. It is an ordinary macro (click it to see the loop), so it sees locals without capturing them. It uses a GNU statement expression, so it can't initialize a namespace-scope variable; use `zeno::timeit([]{ return expr; })` there.
- **Disassembly per cell:** `asm` in the cell toolbar. Hovering an instruction highlights its source line.
- **Crashes** print a gdb backtrace with locals, pointing at the cell's file:line, then roll back.
- **`zeno::here()`** prints a gdb snapshot of the stack with locals, like a breakpoint that keeps going.
- **Heap panel:**
  - live bytes by allocation site and by run
  - an address-ordered heap map
  - a valgrind-style leak check (blocks unreachable from any variable)
- **Variables panel:** everything defined, inspectable.
- **Macros:** click any macro use to expand it in place, highlighted. Click again to collapse.

## Editing, git, hot reload

- **Keys:** one thing is selected at a time: a cell, a comment or a group header.
  - `j`/`k` (or ↓/↑) move; `h`/`l` (or ←/→) fold and unfold groups and move up or into a level; `g`/`G` jump to the top or bottom.
  - Enter edits the selection in place, `e` in your editor.
  - Shift+Enter brings a cell up to date (on a group, its last cell); `r`, `R` and `A` run it in the other ways above.
  - `v` shows or hides a cell's value (its memory layout, for a struct), `m` its assembly.
  - `o`/`O` open an empty editor below/above, `dd` deletes a cell or comment, `u` undoes the last delete.
  - `c` hands the selection to the assistant; `x` marks cells to move into a new file together (*Move selected to file…*); `?` lists the keys.
  - Toolbar buttons underline their key.
- **Editing:** Enter (or a click on code, or a double-click on a comment) edits the selection in place. On a cell that's the cell; on a comment, its `//` source; on a section, function or file header, all of it in one buffer. Esc or clicking away saves, Shift+Enter saves and syncs, Ctrl+Space asks the model for a completion. Saving writes exactly that range of the file, and refuses if the file changed underneath. In the terminal REPL, `.edit N` opens `$EDITOR`.
- **Your editor:** `e` does the same in your editor, running in a terminal embedded in the page: `ZENO_EDITOR`, else `$EDITOR`, else vim. Vim also gets model completion on Ctrl-Space and a terminal that grows with the buffer (vim reports its line count through an escape sequence).
- **Hot reload:** files changed on disk by an editor, a coding agent or git reload in place. Outputs stay with the right cells: cell lists are aligned diff-style, so inserting a cell doesn't misattribute outputs. Edited cells keep their old output, marked stale.
- **Git:** the header shows the branch, and cells that differ from HEAD get a change bar.
- **Grouping:** select cells (Shift-click the gutter, or `x`) and use **Move to file…** to put them in a new header, replaced by an `#include`.
- **Keys:** `j`/`k` move between cells, Shift+Enter runs, `b` inserts a cell below.

## Language models

The **Assistant** panel chats about the notebook. It sees the outline, the focused cell and its last output. `c` on a cell, comment or section shows that selection as source and asks the assistant to rewrite it; its proposal arrives as a code block with an **Apply** button that replaces exactly that range. It works with any OpenAI-compatible server: llama.cpp, Ollama, vLLM, LM Studio, OpenRouter, OpenAI.

Autocomplete asks thinking models (Gemma, Qwen3…) to skip reasoning with `chat_template_kwargs`, which llama.cpp, vLLM and Ollama understand; servers that reject the field get a plain retry. The chat keeps reasoning on and shows "thinking…" meanwhile.

To configure it, either:
- use *settings* in the Assistant panel (saved to `~/.config/zeno/config.json`), or
- set `ZENO_MODEL_URL=http://host:8080/v1 ZENO_MODEL=name ZENO_API_KEY=…`.

## For coding agents (MCP)

`zeno mcp <project>` connects to that project's web server, starting one in the background if needed. The browser therefore shows an agent's runs live. Tools:

| Tool | What it does |
|---|---|
| `outline` | files, includes, headings, and every cell with its uid and first line |
| `list_files`, `read_file`, `write_file` | the project folder (read-only paths are enforced) |
| `read_cell`, `edit_cell`, `insert_cell`, `delete_cell` | cells by uid, written back into their files |
| `run` | Run cells: `mode` sync (default), this, deps, above or all; `group` runs a file or section unconditionally |
| `results` | a cell's last output |
| `reveal` | inspect a value from a result's ref |
| `variables`, `heap`, `leaks`, `disassemble`, `layout`, `undo` | the panels above, as text |

Example: `claude mcp add zeno -- /path/to/zeno mcp /path/to/project`.

## How it works

The kernel is a tree of processes. Each cell batch is compiled with g++ (against a precompiled `include/zeno.hpp`, about 0.6–0.9 s per batch) into a shared library:
- Declarations become `inline` variables in nested per-cell namespaces.
- Statements become ordered static initializers.
- Later cells re-include earlier declarations; the dynamic linker binds them, and their init guards, to the original instances.

The tip process `fork()`s and the child `dlopen()`s the library on a fresh mmap'd stack, which also lets `mincore` measure peak stack use. On success the child becomes the new tip and its parent stays behind as a copy-on-write snapshot. `malloc`/`new` are interposed for heap tracking. Value inspection is template code in `include/zeno.hpp` (struct field names come from `__PRETTY_FUNCTION__`). Pointer expansions run in a throwaway fork so wild pointers can't hurt the state.

| File | Role |
|---|---|
| `include/zeno.hpp` | runtime every cell sees: inspection, plots, timeit, display |
| `src/cell.cpp` | statement splitter, classifier, code generation, dependency analysis |
| `src/kernel.cpp` | compile, fork/snapshot chain, crash reports, asm, macros, type preview |
| `src/runtime.cpp` | heap tracking, leak check, symbols, perf counters, SVG and braille plots |
| `src/project.cpp` | folders/files/includes/`main()` → cell tree, cell editing, read-only rules |
| `src/web.cpp`, `src/ui.cpp` | HTTP server (sync planner, git, vim ptys, assistant) and the embedded page |
| `src/tui.cpp`, `src/mcp.cpp`, `src/ai.cpp` | terminal REPL, MCP server, model client |

## Limitations

- **Linux and g++ only**, for now: fork, dlopen, perf_event and gdb. Field-name reflection needs aggregates with at most 16 fields and no bitfields or C arrays; other types fall back to bytes plus the gdb layout.
- **Some code must sit at global scope** and can't live in a cell's namespace, for example `template<> struct std::hash<…>`. `#if` blocks work; include guards are stripped.
- **Lifetimes:**
  - A cell's value that refers to a temporary dangles (as with `auto&&` in C++).
  - `return` inside `main` ends that cell, not the program.
- **Rebuilding a variable re-runs its declaration**, so an expensive load re-runs when a later cell's mutation of it has to be undone.
- **Effects outside the process** (files written, network) aren't tracked or undone.
- **The terminal REPL** has `.run`, `.edit`, `.tree`, `.x`, `.asm`, `.heap`, `.leaks`, `.layout`, `.undo`. Sync, the assistant and git markers are in the web UI.
