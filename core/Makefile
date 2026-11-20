# Builds and runs the framework-agnostic core protocol tests.
# (The JUCE plugin build uses CMake; this Makefile is just for the pure-C++ core.)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Iinclude -O2

SRC  := src/Parameters.cpp src/Patch.cpp src/SyxFile.cpp src/VoiceEngine.cpp src/Asid.cpp src/AsidVoicePlayer.cpp src/Lfo.cpp src/WaveTable.cpp
TEST := tests/tests.cpp
BIN  := build/tests

.PHONY: test test-san clean
test: $(BIN)
	./$(BIN)

$(BIN): $(SRC) $(TEST) $(wildcard include/sidstation/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) $(TEST) -o $(BIN)

# The same tests with the address and undefined behaviour sanitizers, which the
# sanitizers job in CI runs. The run fails on the first report rather than
# carrying on, since nothing reads this log but a person.
SAN_BIN   := build/tests-san
SAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all \
             -fno-omit-frame-pointer -g

test-san: $(SAN_BIN)
	./$(SAN_BIN)

$(SAN_BIN): $(SRC) $(TEST) $(wildcard include/sidstation/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SAN_FLAGS) $(SRC) $(TEST) -o $(SAN_BIN)

clean:
	rm -rf build
