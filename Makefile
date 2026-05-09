# Project 2 (Part 2): Tool Registry & Parallel Execution.
#
# Layout assumptions:
#   - All Part 1 carry-over files (main.c, config, http, message, util, ui)
#     live next to this Makefile, unchanged.
#   - Phase A introduces tools/ — registry, sandbox, executor, bash (reading
#     material), plus contracts for the three file tools you create.
#   - Phase B implementation lives in tools/executor.c. If students add
#     helper sources, pass them through EXTRA_SRCS / EXTRA_CFLAGS.
#
# Build modes:
#   make          default, fully optimized for development (-g -O0)
#   make asan     same files, AddressSanitizer + UBSan
#   make tsan     same files, ThreadSanitizer
#
# Test modes:
#   make test          all phases via tests/run_tests.py
#   make test-a        Phase A only
#   make test-b        Phase B only
#   make test-asan     run all phases under ASan
#   make test-tsan     run the C-level executor test under TSan
#   make test-cunit    pure-C unit tests (registry, sandbox, executor races)
#
CC      ?= cc
EXTRA_CFLAGS ?=
EXTRA_SRCS ?=
CFLAGS  := -D_XOPEN_SOURCE=700 -std=c11 -Wall -Wextra -pedantic \
           -Wno-format-truncation -g -MMD -MP -I. -Ilibs $(EXTRA_CFLAGS)
# 静音warning snprintf 拼接 workdir + "/" + rel_path 时，编译器理论上算出两个字符串加起来可能超过4096字节会截断。但实际使用中路径根本不会那么长，snprintf 截断也是安全行为
# 还是不修改TA的sandbox比较好的
LDLIBS  := -lpthread

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer

# Source partitioning ---------------------------------------------------------

AGENT_SRCS := $(sort $(wildcard agent/*.c))
TOOL_SRCS  := $(sort $(wildcard tools/*.c))
UI_SRCS    := $(sort $(wildcard ui/*.c))

CORE_SRCS  := main.c cmd.c session.c config.c message.c util.c http.c \
              $(AGENT_SRCS) $(TOOL_SRCS) $(UI_SRCS)

SRCS := $(CORE_SRCS) $(EXTRA_SRCS)

BUILD     := build
OBJS      := $(SRCS:%.c=$(BUILD)/%.o) $(BUILD)/cJSON.o
DEPS      := $(SRCS:%.c=$(BUILD)/%.d) $(BUILD)/cJSON.d
TARGET    := $(BUILD)/c-agent
ASAN_TGT  := $(BUILD)/c-agent-asan
TSAN_TGT  := $(BUILD)/c-agent-tsan

.PHONY: all clean clean-objs \
        test test-a test-b test-asan test-tsan test-agent-tsan \
        test-cunit test-cunit-parallel test-cunit-tsan \
        asan tsan

# ── Default: build the agent binary ──────────────────────────────────────

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/cJSON.o: libs/cJSON.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Sanitizer builds (separate target, separate objects) ──────────────────

asan: CFLAGS += $(ASAN_FLAGS)
asan: clean-objs $(ASAN_TGT)

$(ASAN_TGT): $(OBJS)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $^ $(LDLIBS)

tsan: CFLAGS += $(TSAN_FLAGS)
tsan: clean-objs $(TSAN_TGT)

$(TSAN_TGT): $(OBJS)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o $@ $^ $(LDLIBS)

# ── Test entry points ────────────────────────────────────────────────────

test: all
	python3 tests/run_tests.py --bin ./$(TARGET)

test-a: all
	python3 tests/run_tests.py --bin ./$(TARGET) --phase a

test-b: all
	python3 tests/run_tests.py --bin ./$(TARGET) --phase b

test-asan: asan
	python3 tests/run_tests.py --bin ./$(ASAN_TGT)

test-tsan: test-cunit-tsan

test-agent-tsan: tsan
	python3 tests/run_tests.py --bin ./$(TSAN_TGT) --phase b

# Pure-C unit tests live in tests/cunit/. They link directly against the
# tool registry and executor so TSan can see the relevant pthread calls
# without going through the Python harness.
#
#   test_registry, test_sandbox: pure logic, no threads. Run as part of the
#                                Phase A workflow.
#   test_executor:               drives the parallel executor under
#                                ASan/TSan. Depends on the Phase A.6
#                                read_only field and the Phase B executor
#                                edit.

define cunit_link
$(CC) $(CFLAGS) $(1) tests/cunit/$(2).c \
    config.c message.c util.c \
    tools/registry.c tools/sandbox.c \
    tools/bash.c tools/read.c tools/write.c tools/edit.c \
    libs/cJSON.c \
    -o $(BUILD)/cunit-$(3)/$(2) $(LDLIBS)
endef

define cunit_link_executor
$(CC) $(CFLAGS) $(1) tests/cunit/test_executor.c \
    config.c message.c util.c \
    tools/registry.c tools/sandbox.c tools/executor.c \
    tools/bash.c tools/read.c tools/write.c tools/edit.c \
    ui/ui.c ui/render.c \
    libs/cJSON.c $(EXTRA_SRCS) \
    -o $(BUILD)/cunit-$(2)/test_executor $(LDLIBS)
endef

test-cunit:
	@mkdir -p $(BUILD)/cunit-asan
	@for bn in test_registry test_sandbox; do \
	  echo "  CC   $$bn (asan)"; \
	  $(call cunit_link,$(ASAN_FLAGS),$$bn,asan) || exit 1; \
	  $(BUILD)/cunit-asan/$$bn || exit 1; \
	done

test-cunit-parallel:
	@mkdir -p $(BUILD)/cunit-asan
	@echo "  CC   test_executor (asan)"
	@$(call cunit_link_executor,$(ASAN_FLAGS),asan)
	@$(BUILD)/cunit-asan/test_executor

test-cunit-tsan:
	@mkdir -p $(BUILD)/cunit-tsan
	@echo "  CC   test_executor (tsan)"
	@$(call cunit_link_executor,$(TSAN_FLAGS),tsan)
	@$(BUILD)/cunit-tsan/test_executor

# ── Cleanup ──────────────────────────────────────────────────────────────

clean-objs:
	rm -rf $(BUILD)

clean: clean-objs

-include $(DEPS)
