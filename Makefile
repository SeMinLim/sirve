CXX ?= g++
CPPFLAGS ?=
CXXFLAGS ?= -std=c++11 -g -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?=

SOURCES := cachesim.cpp emulator.cpp
HEADERS := cachesim.h linenoise.hpp
TARGET := obj/emulator
ASAN_TARGET := obj/emulator-asan
UBSAN_TARGET := obj/emulator-ubsan
TEST_SCRIPT := tests/run_tests.sh

.PHONY: all clean test test-asan test-ubsan check

all: $(TARGET)

obj:
	mkdir -p obj

$(TARGET): $(SOURCES) $(HEADERS) | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

$(ASAN_TARGET): $(SOURCES) $(HEADERS) | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
		$(SOURCES) $(LDFLAGS) -fsanitize=address $(LDLIBS) -o $@

$(UBSAN_TARGET): $(SOURCES) $(HEADERS) | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined \
		$(SOURCES) $(LDFLAGS) -fsanitize=undefined $(LDLIBS) -o $@

test: $(TARGET)
	$(TEST_SCRIPT) $(TARGET)

test-asan: $(ASAN_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(TEST_SCRIPT) $(ASAN_TARGET)

test-ubsan: $(UBSAN_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(TEST_SCRIPT) $(UBSAN_TARGET)

check:
	$(MAKE) test
	$(MAKE) test-asan
	$(MAKE) test-ubsan

clean:
	rm -rf obj
