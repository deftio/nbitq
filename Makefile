# nbitq -- standalone. No dependencies, no configuration, no generated files.
#
#   make check          everything below except coverage and targets, in order
#   make test           core test suite
#   make test-container the optional .nbq container
#   make sanitize       both suites under ASan + UBSan
#   make fuzz           randomised, fixed seed, runs anywhere
#   make cxx            the headers and nbitq.hpp compiled, linked and run as C++
#   make nomalloc       the core with no allocator and no hosted header
#   make size           code size per optimisation level
#   make coverage       every line of the library executed by the tests, or fail
#   make targets        cross-compile freestanding for embedded targets
#
#   ./release.sh        all of the above plus cmake, as CI runs it; see CONTRIBUTING.md
#
# Copyright (c) 2026 deftio / M. A. Chatterjee, BSD-2-Clause.

CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -pedantic
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer

# The tests ask the allocator for sizes it must refuse, to cover the paths
# that handle a refusal. ASan aborts on such a request unless told to return
# NULL like a normal allocator.
ASANENV := ASAN_OPTIONS=allocator_may_return_null=1

CORE    := nbitq.c
CONT    := nbitq_container.c

# llvm-size is not always on PATH, and plain size cannot read a cross-target
# object; xcrun finds it on macOS.
RESDIR   := $(shell clang -print-resource-dir 2>/dev/null)
LLVMSIZE := $(shell command -v llvm-size 2>/dev/null || echo "xcrun llvm-size")

.PHONY: all check test test-container sanitize fuzz cxx nomalloc size coverage coverage-tool targets clean

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
	$(ASANENV) ./nbitq_test_asan
	$(CC) -O1 -g -std=c99 -Wall -Wextra $(SAN) -o nbitq_container_asan \
	    test_nbitq_container.c $(CONT) $(CORE)
	$(ASANENV) ./nbitq_container_asan

fuzz: fuzz_nbitq.c $(CORE) nbitq.h
	$(CC) -O1 -g -std=c99 -Wall -Wextra $(SAN) -DNBQ_FUZZ_STANDALONE \
	    -o nbitq_fuzz fuzz_nbitq.c $(CORE)
	./nbitq_fuzz 100000

# The headers promise extern "C" and nbitq.hpp promises to be a thin forwarder.
# That is only true if someone compiles and runs them that way, so this is a
# test and not a comment.
cxx: test_nbitq_cxx.cpp nbitq.h nbitq.hpp nbitq_container.h $(CORE) $(CONT)
	$(CC) $(CFLAGS) -c $(CORE) -o nbitq.o
	$(CC) $(CFLAGS) -c $(CONT) -o nbitq_container.o
	$(CXX) -std=c++11 -Wall -Wextra -pedantic -o nbitq_cxx test_nbitq_cxx.cpp nbitq.o nbitq_container.o
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

# ── Coverage ──────────────────────────────────────────────────────────────
# Every test binary runs against one instrumented build of the library, so the
# counts merge, and every line of nbitq.c, nbitq_container.c, nbitq.h and
# nbitq.hpp must have run or the target fails. gcov is the compiler's own
# tool. gcc ships gcov, named like the compiler (gcc-14 pairs with gcov-14),
# which is what CI uses on Linux and what MSYS2 or WSL provide on Windows.
# clang spells it `llvm-cov gcov`; on macOS, where the system gcov is itself
# Apple's llvm wrapper, xcrun finds it. Set GCOV= to override. Lines marked
# LCOV_EXCL_LINE in the source are skipped; each carries its reason.
COVDIR   := build-coverage
COVFLAGS := -O0 -g --coverage -Wall -Wextra
GCOV     ?= $(shell if $(CC) --version 2>/dev/null | grep -qi clang; then \
                echo "$$(command -v llvm-cov 2>/dev/null || echo xcrun llvm-cov) gcov"; \
            elif echo "$(CC)" | grep -q gcc; then echo "$(CC)" | sed "s/gcc/gcov/"; \
            else echo gcov; fi)

coverage-tool:
	@$(GCOV) --version >/dev/null 2>&1 || { echo "no gcov tool for $(CC): tried '$(GCOV)'"; exit 2; }

coverage: coverage-tool nbitq.h nbitq.hpp $(CORE) $(CONT) test_nbitq.c test_nbitq_container.c fuzz_nbitq.c test_nbitq_cxx.cpp
	@rm -rf $(COVDIR) && mkdir -p $(COVDIR)
	$(CC) $(COVFLAGS) -std=c99 -c $(CORE) -o $(COVDIR)/nbitq.o
	$(CC) $(COVFLAGS) -std=c99 -c $(CONT) -o $(COVDIR)/nbitq_container.o
	$(CXX) $(COVFLAGS) -std=c++11 -c test_nbitq_cxx.cpp -o $(COVDIR)/test_nbitq_cxx.o
	$(CC) $(COVFLAGS) -std=c99 -o $(COVDIR)/t_core test_nbitq.c $(COVDIR)/nbitq.o
	$(CC) $(COVFLAGS) -std=c99 -o $(COVDIR)/t_container test_nbitq_container.c \
	    $(COVDIR)/nbitq_container.o $(COVDIR)/nbitq.o
	$(CC) $(COVFLAGS) -std=c99 -DNBQ_FUZZ_STANDALONE -o $(COVDIR)/t_fuzz fuzz_nbitq.c $(COVDIR)/nbitq.o
	$(CXX) $(COVFLAGS) -std=c++11 -o $(COVDIR)/t_cxx $(COVDIR)/test_nbitq_cxx.o \
	    $(COVDIR)/nbitq.o $(COVDIR)/nbitq_container.o
	$(COVDIR)/t_core && $(COVDIR)/t_container && $(COVDIR)/t_fuzz 20000 && $(COVDIR)/t_cxx
	@# The C sources first, then the C++ test: that is the translation unit
	@# that instantiates every inline function in the headers, so its
	@# nbitq.h.gcov and nbitq.hpp.gcov are the ones that count.
	@$(GCOV) -b -o $(COVDIR) $(CORE) $(CONT) > $(COVDIR)/gcov.log 2>&1 \
	    && $(GCOV) -b -o $(COVDIR) test_nbitq_cxx.cpp >> $(COVDIR)/gcov.log 2>&1 \
	    && mv *.gcov $(COVDIR)/
	@cd $(COVDIR) && hit=0 && total=0 && fail=0 && : > coverage.txt && \
	for f in $(CORE) $(CONT) nbitq.h nbitq.hpp; do \
	    [ -f "$$f.gcov" ] || { echo "coverage: no $$f.gcov; nothing in $$f was compiled in"; exit 1; }; \
	    set -- $$(awk -F: '/LCOV_EXCL_LINE/ { next } \
	                      $$1 ~ /^ *[0-9]+\*?$$/     { t++; h++ } \
	                      $$1 ~ /^ *(#####|=====)$$/ { t++ } \
	                      END { printf "%d %d\n", h, t }' "$$f.gcov"); \
	    br=$$(grep -c '^branch' "$$f.gcov"); \
	    br0=$$(grep -cE '^branch .*(taken 0|never executed)' "$$f.gcov"); \
	    pct=$$(awk -v h=$$1 -v t=$$2 'BEGIN { printf "%.1f", (t ? 100*h/t : 0) }'); \
	    printf "  %-20s %6s%% of lines  (%3d of %3d)   branches %3d of %3d taken\n" \
	        "$$f" "$$pct" "$$1" "$$2" "$$((br-br0))" "$$br" | tee -a coverage.txt; \
	    hit=$$((hit+$$1)); total=$$((total+$$2)); \
	    [ "$$1" -eq "$$2" ] || { fail=1; grep -nE '^ *(#####|=====):' "$$f.gcov" | sed "s|^|    $$f.gcov:|"; }; \
	done; \
	pct=$$(awk -v h=$$hit -v t=$$total 'BEGIN { printf "%.1f", (t ? 100*h/t : 0) }'); \
	printf "TOTAL %d %d %s\n" "$$hit" "$$total" "$$pct" >> coverage.txt; \
	printf "  %-20s %6s%% of lines  (%3d of %3d)\n" "total" "$$pct" "$$hit" "$$total"; \
	if [ "$$fail" -ne 0 ]; then echo "coverage: the lines above never ran; the gate is 100%"; exit 1; fi

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
	      .size.o .t.o *.gcov
	rm -rf *.dSYM $(COVDIR)
