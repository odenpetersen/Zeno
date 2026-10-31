// # An ordinary program, opened as a notebook
//
// `main`'s statements are cells: run them one at a time and inspect the locals.

#include <iostream>
#include <map>
#include <string>

std::map<std::string, int> count_words(const std::string& text) {
  std::map<std::string, int> m;
  std::string w;
  for (char c : text + " ") {
    if (std::isalpha((unsigned char)c)) w += std::tolower((unsigned char)c);
    else if (!w.empty()) { ++m[w]; w.clear(); }
  }
  return m;
}

int main(int argc, char** argv) {
  // ## Input
  std::string text = "the quick brown fox jumps over the lazy dog the end";

  // ## Count
  auto counts = count_words(text);
  std::cout << counts.size() << " distinct words\n";

  // ## Report
  for (auto& [w, n] : counts)
    if (n > 1) std::cout << w << ": " << n << "\n";
  return 0;
}
