// # A C++ notebook
//
// This file is the entry point. Comments on their own lines render as **markdown**,
// every top-level statement is a cell, and each local `#include` becomes a
// collapsible group holding that file's cells.

#include <cmath>
#include "geometry.hpp"
#include "data.hpp"

// ## Using what the includes defined

Vec3 a{1, 2, 3}, b{4, 5, 6};
dot(a, b);
auto c = cross(a, b);

// Values are kept but hidden; reveal one to see its type, layout and padding.
Particle p{a, 2.5, 'x'};

// ## Redefinition
//
// Cells can be re-run and names redeclared; the newest declaration wins.

double scale = 2;
double scale = 3.5;
scale * 2;

// ## Statements are cells too

for (int i = 0; i < 3; ++i)
  std::cout << "step " << i << "\n";

if (scale > 3) {
  std::cout << "scale is large\n";
}

// ## Macros
//
// Click `SQUARE` in the web view to see its expansion in place.

#define SQUARE(x) ((x) * (x))
SQUARE(scale + 1);

// ## Plots and timing

zeno::plot(samples).title("noisy sine");
//%timeit std::accumulate(samples.begin(), samples.end(), 0.0)

// ## Memory
//
// Pointers expand on demand and know which heap block they point to.

struct Node { int val; Node* next; };
Node* list = new Node{1, new Node{2, new Node{3, nullptr}}};
int* arr = new int[8]{1, 2, 3, 4, 5, 6, 7, 8};
