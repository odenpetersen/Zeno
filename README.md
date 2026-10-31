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
| `zeno web [path] [--host 0.0.0.0] [--port N]` | browser notebook; `--host 0.0.0.0` makes it reachable from your network |
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
  - `//%timeit EXPR`
  - `//%flags -O3` (this cell)
  - `//%cflags …` (this and later cells)
  - `//%link -lfoo` or `//%link other.cpp`
  - `//%check` (UBSan and libstdc++ assertions)
  - `//%asm`
- **Read-only parts:** list globs in `.zeno.json` at the project root, e.g. `{"readonly": ["vendor/**", "third_party/**"]}`. Edits from the page, vim and agents are refused there.

## Running: always consistent, never "Run all"

- **Sync** (⟳, and ▶ on a cell for "up to here") re-runs exactly what's out of date. That means cells that changed or are new, plus every later cell that reads a name they write, transitively. The state therefore always equals running the files top to bottom.
- **Rollbacks:** when re-running in place would be wrong (a mutation was edited, a cell was deleted, or a later cell changed something an earlier one reads), Sync first rolls back to an earlier snapshot.
- **Dependencies** are name-level: definitions and identifiers from the cell tokenizer. Statements and initializers are assumed to modify whatever they mention, so Sync may re-run too much but never too little.
- **Every run is a transaction.** Each run executes in a `fork()` of the previous state. A crash, exception or Ctrl-C/Interrupt discards the child, and the state is exactly as before. Undo steps back one run.
- **Redeclaration works:** every cell gets a nested namespace, so the newest declaration of a name wins.

## Seeing values

- **Results are hidden until revealed.** Each value-producing cell shows its type with a *reveal* link. Before a cell has ever run, the compiler supplies its type and memory layout ("not run · layout").
- **Structs** show each field's offset, size, type and value, with padding rows (the CLion-style memory layout).
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
- **Benchmarks:** `zeno::timeit(f)` / `//%timeit` report mean ± std per loop, with cycles and instructions.
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

- **Editing:** Enter or double-click opens vim on just that cell, in a terminal embedded in the cell. The terminal grows with the buffer, because vim reports its line count through an escape sequence. `:wq` writes the cell back into its file. In the terminal REPL, `.edit N` opens `$EDITOR`.
- **Autocomplete in vim:** Ctrl-Space (or Ctrl-X Ctrl-U) asks the configured model.
- **Hot reload:** files changed on disk by an editor, a coding agent or git reload in place. Outputs stay with the right cells: cell lists are aligned diff-style, so inserting a cell doesn't misattribute outputs. Edited cells keep their old output, marked stale.
- **Git:** the header shows the branch, and cells that differ from HEAD get a change bar.
- **Grouping:** select cells (Shift-click the gutter, or `x`) and use **Move to file…** to put them in a new header, replaced by an `#include`.
- **Keys:** `j`/`k` move between cells, Shift+Enter runs, `b` inserts a cell below.

## Language models

The **Assistant** panel (and `ask` on any cell) chats about the notebook. It sees the outline, the focused cell and its last output. Proposed edits arrive as code blocks with **Apply** buttons. It works with any OpenAI-compatible server: llama.cpp, Ollama, vLLM, LM Studio, OpenRouter, OpenAI.

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
| `run` | Sync up to a cell, or everything; `group` / `force` run cells unconditionally |
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
- **Rollback granularity is one run:** one Sync or ▶ is one snapshot. A rollback re-runs everything after that snapshot.
- **The terminal REPL** has `.run`, `.edit`, `.tree`, `.x`, `.asm`, `.heap`, `.leaks`, `.layout`, `.undo`. Sync, the assistant and git markers are in the web UI.
