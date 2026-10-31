// Zeno — a C++ notebook. Entry point.
#include "common.hpp"
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  std::string cmd = argc > 1 ? argv[1] : "", nb;
  if (cmd == "--kernel") return kernel_main();
  int port = 8888; bool open = true, token = false; std::string host = "127.0.0.1";
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--no-browser") open = false;
    else if (a == "--token") token = true;
    else if ((a == "--host" || a == "--ip") && i + 1 < argc) host = argv[++i];
    else nb = a;
  }
  if (cmd == "web") return web_main(nb, port, open, host, token);
  if (cmd == "mcp") return mcp_main(nb);
  if (cmd == "new") return new_main(nb);
  if (cmd == "doctor") return doctor_main();
  if (cmd == "run" && !nb.empty()) return run_main(nb);
  if (cmd == "repl" || cmd.empty()) return tui_main(nb);
  if (cmd.size() > 4 && cmd.substr(cmd.size() - 4) == ".cpp") return tui_main(cmd);
  std::printf("usage: zeno new [dir]                    start a notebook (writes a short tour to dir/main.cpp)\n"
              "       zeno doctor                      check the toolchain and warm up the compiler cache\n"
              "       zeno [repl] [notebook.cpp]      interactive terminal notebook\n"
              "       zeno run notebook.cpp           run all cells, print outputs\n"
              "       zeno web [notebook.cpp] [--port N] [--host IP] [--token] [--no-browser]\n"
              "                                       browser notebook (--host 0.0.0.0: whole network; --token: require a secret in the URL)\n"
              "       zeno mcp [path]                 MCP server (stdio) for coding agents\n");
  return cmd == "-h" || cmd == "--help" ? 0 : 2;
}
