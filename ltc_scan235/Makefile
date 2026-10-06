CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -Ithird_party -Isrc
BUILD    := build

CORE_SRCS := src/audio.cpp src/biphase.cpp src/ltc_frame.cpp src/segments.cpp

all: $(BUILD)/ltc_scan235 $(BUILD)/gen_wav

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/ltc_scan235: $(CORE_SRCS) src/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(CORE_SRCS) src/main.cpp

$(BUILD)/gen_wav: tools/gen_wav.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ tools/gen_wav.cpp

test: all
	./tests/run_tests.sh

clean:
	rm -rf $(BUILD)

.PHONY: all test clean

