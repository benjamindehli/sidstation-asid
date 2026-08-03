# Builds and runs the framework-agnostic core protocol tests.
# (The JUCE plugin build uses CMake; this Makefile is just for the pure-C++ core.)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Iinclude -O2

SRC  := src/Parameters.cpp src/Patch.cpp src/SyxFile.cpp src/VoiceEngine.cpp src/Asid.cpp src/AsidVoicePlayer.cpp src/Lfo.cpp
TEST := tests/tests.cpp
BIN  := build/tests

.PHONY: test clean
test: $(BIN)
	./$(BIN)

$(BIN): $(SRC) $(TEST) $(wildcard include/sidstation/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) $(TEST) -o $(BIN)

clean:
	rm -rf build
