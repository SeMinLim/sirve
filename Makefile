CXX ?= g++
CPPFLAGS ?=
CPPFLAGS += -Isrc
CXXFLAGS ?= -std=c++11 -g -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?=

SOURCES := src/cache.cpp src/rv32i.cpp src/sirve.cpp
HEADERS := src/cache.h src/linenoise.hpp src/rv32i.h
TARGET := obj/sirve
ASAN_TARGET := obj/sirve-asan
UBSAN_TARGET := obj/sirve-ubsan
DECODE_TEST_SOURCE := tests/rv32i_decode_test.cpp
DECODE_TEST_TARGET := obj/rv32i_decode_test
ASAN_DECODE_TEST_TARGET := obj/rv32i_decode_test-asan
UBSAN_DECODE_TEST_TARGET := obj/rv32i_decode_test-ubsan
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

$(DECODE_TEST_TARGET): $(DECODE_TEST_SOURCE) src/rv32i.cpp src/rv32i.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DECODE_TEST_SOURCE) src/rv32i.cpp \
		$(LDFLAGS) $(LDLIBS) -o $@

$(ASAN_DECODE_TEST_TARGET): $(DECODE_TEST_SOURCE) src/rv32i.cpp src/rv32i.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
		$(DECODE_TEST_SOURCE) src/rv32i.cpp $(LDFLAGS) -fsanitize=address $(LDLIBS) -o $@

$(UBSAN_DECODE_TEST_TARGET): $(DECODE_TEST_SOURCE) src/rv32i.cpp src/rv32i.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined \
		$(DECODE_TEST_SOURCE) src/rv32i.cpp $(LDFLAGS) -fsanitize=undefined $(LDLIBS) -o $@

test: $(TARGET) $(DECODE_TEST_TARGET)
	$(DECODE_TEST_TARGET)
	$(TEST_SCRIPT) $(TARGET)

test-asan: $(ASAN_TARGET) $(ASAN_DECODE_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(ASAN_DECODE_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(TEST_SCRIPT) $(ASAN_TARGET)

test-ubsan: $(UBSAN_TARGET) $(UBSAN_DECODE_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(UBSAN_DECODE_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(TEST_SCRIPT) $(UBSAN_TARGET)

check:
	$(MAKE) test
	$(MAKE) test-asan
	$(MAKE) test-ubsan

clean:
	rm -rf obj
