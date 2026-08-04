CXX ?= g++
CPPFLAGS ?=
CPPFLAGS += -Isrc
CXXFLAGS ?= -std=c++11 -g -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?=

SOURCES := src/assembler.cpp src/cache.cpp src/loader.cpp src/memory.cpp src/rv32i.cpp src/sirve.cpp
HEADERS := src/assembler.h src/cache.h src/linenoise.hpp src/loader.h src/memory.h src/rv32i.h
TARGET := obj/sirve
ASAN_TARGET := obj/sirve-asan
UBSAN_TARGET := obj/sirve-ubsan

DECODE_TEST_TARGET := obj/rv32i_decode_test
ASSEMBLER_TEST_TARGET := obj/assembler_test
EXECUTE_TEST_TARGET := obj/rv32i_execute_test
LOADER_TEST_TARGET := obj/loader_test
ELF_LOADER_TEST_TARGET := obj/elf_loader_test
ASAN_DECODE_TEST_TARGET := obj/rv32i_decode_test-asan
ASAN_ASSEMBLER_TEST_TARGET := obj/assembler_test-asan
ASAN_EXECUTE_TEST_TARGET := obj/rv32i_execute_test-asan
ASAN_LOADER_TEST_TARGET := obj/loader_test-asan
ASAN_ELF_LOADER_TEST_TARGET := obj/elf_loader_test-asan
UBSAN_DECODE_TEST_TARGET := obj/rv32i_decode_test-ubsan
UBSAN_ASSEMBLER_TEST_TARGET := obj/assembler_test-ubsan
UBSAN_EXECUTE_TEST_TARGET := obj/rv32i_execute_test-ubsan
UBSAN_LOADER_TEST_TARGET := obj/loader_test-ubsan
UBSAN_ELF_LOADER_TEST_TARGET := obj/elf_loader_test-ubsan
TEST_SCRIPT := tests/run_tests.sh
SPIKE_TEST_SCRIPT := tests/run_spike_diff_test.sh
SPIKE_DIFF_ELF := obj/spike-diff-rv32i.elf
TOOLCHAIN_TEST_SCRIPT := tests/run_gnu_toolchain_test.sh
FREESTANDING_ELF := obj/freestanding-rv32i.elf

.PHONY: all clean test test-asan test-ubsan test-toolchain test-spike check

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

$(DECODE_TEST_TARGET): tests/rv32i_decode_test.cpp src/rv32i.cpp src/rv32i.h src/memory.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/rv32i_decode_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) $(LDLIBS) -o $@

$(ASSEMBLER_TEST_TARGET): tests/assembler_test.cpp src/assembler.cpp src/assembler.h src/rv32i.cpp src/rv32i.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/assembler_test.cpp src/assembler.cpp src/rv32i.cpp \
		src/memory.cpp src/cache.cpp $(LDFLAGS) $(LDLIBS) -o $@

$(EXECUTE_TEST_TARGET): tests/rv32i_execute_test.cpp src/rv32i.cpp src/rv32i.h src/memory.cpp src/memory.h src/cache.cpp src/cache.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/rv32i_execute_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) $(LDLIBS) -o $@

$(LOADER_TEST_TARGET): tests/loader_test.cpp src/loader.cpp src/loader.h src/memory.cpp src/memory.h src/cache.cpp src/cache.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/loader_test.cpp src/loader.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) $(LDLIBS) -o $@

$(ELF_LOADER_TEST_TARGET): tests/elf_loader_test.cpp src/loader.cpp src/loader.h \
                          src/rv32i.cpp src/rv32i.h src/memory.cpp src/memory.h \
                          src/cache.cpp src/cache.h | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/elf_loader_test.cpp src/loader.cpp src/rv32i.cpp \
		src/memory.cpp src/cache.cpp $(LDFLAGS) $(LDLIBS) -o $@

$(ASAN_DECODE_TEST_TARGET): tests/rv32i_decode_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
		tests/rv32i_decode_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=address $(LDLIBS) -o $@

$(ASAN_ASSEMBLER_TEST_TARGET): tests/assembler_test.cpp src/assembler.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
		tests/assembler_test.cpp src/assembler.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=address $(LDLIBS) -o $@

$(ASAN_EXECUTE_TEST_TARGET): tests/rv32i_execute_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
		tests/rv32i_execute_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=address $(LDLIBS) -o $@

$(ASAN_LOADER_TEST_TARGET): tests/loader_test.cpp src/loader.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
		tests/loader_test.cpp src/loader.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=address $(LDLIBS) -o $@

$(ASAN_ELF_LOADER_TEST_TARGET): tests/elf_loader_test.cpp src/loader.cpp src/rv32i.cpp \
                               src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
		tests/elf_loader_test.cpp src/loader.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=address $(LDLIBS) -o $@

$(UBSAN_DECODE_TEST_TARGET): tests/rv32i_decode_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined \
		tests/rv32i_decode_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=undefined $(LDLIBS) -o $@

$(UBSAN_ASSEMBLER_TEST_TARGET): tests/assembler_test.cpp src/assembler.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined \
		tests/assembler_test.cpp src/assembler.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=undefined $(LDLIBS) -o $@

$(UBSAN_EXECUTE_TEST_TARGET): tests/rv32i_execute_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined \
		tests/rv32i_execute_test.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=undefined $(LDLIBS) -o $@

$(UBSAN_LOADER_TEST_TARGET): tests/loader_test.cpp src/loader.cpp src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined \
		tests/loader_test.cpp src/loader.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=undefined $(LDLIBS) -o $@

$(UBSAN_ELF_LOADER_TEST_TARGET): tests/elf_loader_test.cpp src/loader.cpp src/rv32i.cpp \
                                src/memory.cpp src/cache.cpp | obj
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsanitize=undefined -fno-sanitize-recover=undefined \
		tests/elf_loader_test.cpp src/loader.cpp src/rv32i.cpp src/memory.cpp src/cache.cpp \
		$(LDFLAGS) -fsanitize=undefined $(LDLIBS) -o $@

test: $(TARGET) $(DECODE_TEST_TARGET) $(ASSEMBLER_TEST_TARGET) $(EXECUTE_TEST_TARGET) \
      $(LOADER_TEST_TARGET) $(ELF_LOADER_TEST_TARGET)
	$(DECODE_TEST_TARGET)
	$(ASSEMBLER_TEST_TARGET)
	$(EXECUTE_TEST_TARGET)
	$(LOADER_TEST_TARGET)
	$(ELF_LOADER_TEST_TARGET)
	$(TEST_SCRIPT) $(TARGET)

test-asan: $(ASAN_TARGET) $(ASAN_DECODE_TEST_TARGET) $(ASAN_ASSEMBLER_TEST_TARGET) \
           $(ASAN_EXECUTE_TEST_TARGET) $(ASAN_LOADER_TEST_TARGET) \
           $(ASAN_ELF_LOADER_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(ASAN_DECODE_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(ASAN_ASSEMBLER_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(ASAN_EXECUTE_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(ASAN_LOADER_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(ASAN_ELF_LOADER_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 $(TEST_SCRIPT) $(ASAN_TARGET)

test-ubsan: $(UBSAN_TARGET) $(UBSAN_DECODE_TEST_TARGET) $(UBSAN_ASSEMBLER_TEST_TARGET) \
            $(UBSAN_EXECUTE_TEST_TARGET) $(UBSAN_LOADER_TEST_TARGET) \
            $(UBSAN_ELF_LOADER_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(UBSAN_DECODE_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(UBSAN_ASSEMBLER_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(UBSAN_EXECUTE_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(UBSAN_LOADER_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(UBSAN_ELF_LOADER_TEST_TARGET)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 $(TEST_SCRIPT) $(UBSAN_TARGET)

test-toolchain: $(TARGET)
	$(TOOLCHAIN_TEST_SCRIPT) $(TARGET) $(FREESTANDING_ELF)

test-spike: $(TARGET)
	$(SPIKE_TEST_SCRIPT) $(TARGET) $(SPIKE_DIFF_ELF)

check:
	$(MAKE) test
	$(MAKE) test-asan
	$(MAKE) test-ubsan
	$(MAKE) test-toolchain

clean:
	rm -rf obj
