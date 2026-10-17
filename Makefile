# nbitq -- standalone. No dependencies, no configuration, no generated files.
#
#   make check          everything below, in order
#   make test           core test suite
#   make test-container the optional .nbq container
#   make sanitize       both suites under ASan + UBSan
#   make fuzz           randomised, fixed seed, runs anywhere
#   make cxx            the headers compiled and linked as C++
#   make nomalloc       the core with no allocator and no hosted header
#   make size           code size per optimisation level
#   make targets        cross-compile freestanding for embedded targets
#
#   ./release.sh        all of the above plus cmake, as CI runs it; see CONTRIBUTING.md
#
# Copyright (c) 2026 deftio / M. A. Chatterjee, BSD-2-Clause.

CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -pedantic
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer

CORE    := nbitq.c
CONT    := nbitq_container.c

# llvm-size is not always on PATH, and plain size cannot read a cross-target
# object; xcrun finds it on macOS.
RESDIR   := $(shell clang -print-resource-dir 2>/dev/null)
LLVMSIZE := $(shell command -v llvm-size 2>/dev/null || echo "xcrun llvm-size")

.PHONY: all check test test-container sanitize fuzz cxx nomalloc size targets clean

all: test test-container

test: test_nbitq.c $(CORE) nbitq.h
	$(CC) $(CFLAGS) -o nbitq_test test_nbitq.c $(CORE)
	./nbitq_test

# The container is a separate translation unit so an embedded build can leave
# it out; its tests are separate for the same reason.
test-container: test_nbitq_container.c $(CONT) $(CORE) nbitq_container.h
	$(CC) $(CFLAGS) -o nbitq_container_test test_nbitq_container.c $(CONT) $(CORE)
	./nbitq_container_test

sanitize: test_nbitq.c test_nbitq_container.c $(CORE) $(CONT)
	$(CC) -O1 -g -std=c99 -Wall -Wextra $(SAN) -o nbitq_test_asan test_nbitq.c $(CORE)
	./nbitq_test_asan
	$(CC) -O1 -g -std=c99 -Wall -Wextra $(SAN) -o nbitq_container_asan \
	    test_nbitq_container.c $(CONT) $(CORE)
	./nbitq_container_asan

fuzz: fuzz_nbitq.c $(CORE) nbitq.h
	$(CC) -O1 -g -std=c99 -Wall -Wextra $(SAN) -DNBQ_FUZZ_STANDALONE \
	    -o nbitq_fuzz fuzz_nbitq.c $(CORE)
	./nbitq_fuzz 100000

# The headers promise extern "C". That is only true if someone compiles them
# that way, so this is a test and not a comment.
cxx: test_nbitq_cxx.cpp nbitq.h nbitq_container.h $(CORE) $(CONT)
	$(CC) $(CFLAGS) -c $(CORE) -o nbitq.o
	$(CC) $(CFLAGS) -c $(CONT) -o nbitq_container.o
	$(CXX) -std=c++11 -Wall -Wextra -o nbitq_cxx test_nbitq_cxx.cpp nbitq.o nbitq_container.o
	./nbitq_cxx

# The core must build with no allocator and no hosted header. That is the
# configuration embedded targets use, so it is tested rather than assumed.
nomalloc: $(CORE) nbitq.h
	$(CC) $(CFLAGS) -DNBQ_NO_MALLOC -c $(CORE) -o nbitq_nomalloc.o
	@echo "  NBQ_NO_MALLOC: builds clean"

size:
	@for o in -Oz -Os -O2; do \
	    $(CC) $$o -std=c99 -c $(CORE) -o .size.o 2>/dev/null; \
	    core=$$(size .size.o 2>/dev/null | tail -1 | awk '{print $$1}'); \
	    $(CC) $$o -std=c99 -c $(CONT) -o .size.o 2>/dev/null; \
	    cont=$$(size .size.o 2>/dev/null | tail -1 | awk '{print $$1}'); \
	    printf "  %-4s core %6s   container %6s bytes of .text\n" "$$o" "$$core" "$$cont"; \
	done; rm -f .size.o

# Embedded reality check: freestanding, with NBQ_NO_MALLOC, which needs only
# clang's own resource headers -- no target libc, no sysroot.
targets:
	@for t in thumbv6m-none-eabi thumbv7m-none-eabi thumbv7em-none-eabi \
	          armv7a-none-eabi aarch64-none-elf x86_64-none-elf; do \
	    if clang --target=$$t -Oz -std=c99 -ffreestanding -nostdinc \
	             -isystem "$(RESDIR)/include" -DNBQ_NO_MALLOC \
	             -c $(CORE) -o .t.o 2>/dev/null; then \
	        sz=$$($(LLVMSIZE) .t.o 2>/dev/null | tail -1 | awk '{print $$1}'); \
	        [ -n "$$sz" ] || sz="?"; \
	        printf "  %-22s %6s bytes of .text\n" "$$t" "$$sz"; \
	    else printf "  %-22s (target unsupported by this clang)\n" "$$t"; fi; \
	done; rm -f .t.o

check: test test-container sanitize fuzz cxx nomalloc
	@echo ""
	@$(MAKE) -s size

clean:
	rm -f nbitq_test nbitq_container_test nbitq_test_asan nbitq_container_asan \
	      nbitq_fuzz nbitq_cxx nbitq.o nbitq_container.o nbitq_nomalloc.o \
	      .size.o .t.o
	rm -rf *.dSYM
