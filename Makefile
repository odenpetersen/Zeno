CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wno-sign-compare
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

zeno: $(OBJ)
	$(CXX) -rdynamic -o $@ $(OBJ) -ldl -lpthread

build/%.o: src/%.cpp src/common.hpp src/runtime.hpp include/zeno.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build zeno
.PHONY: clean
