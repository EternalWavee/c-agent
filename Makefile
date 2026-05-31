# Project 2 Part 3
#
# Attention: merge this file with yours.
#
# Build modes:
#   make          development build (-g -O0)
#   make asan     AddressSanitizer + UBSan
#   make tsan     ThreadSanitizer
#
# Test modes:
#   make test           every phase via tests/run_tests.py
#   make test-a/b       Part 2 phases
#   make test-ca/cb     Part 3 phases
#   make test-asan      whole tree under ASan
#   make test-tsan      executor-level race tests
#   make test-cunit     pure-C unit tests
#
CC      ?= cc
EXTRA_CFLAGS ?=
EXTRA_SRCS ?=
CFLAGS  := -D_XOPEN_SOURCE=700 -std=c11 -Wall -Wextra -pedantic \
           -Wno-format-truncation -g -MMD -MP -I. -Ilibs $(EXTRA_CFLAGS)
# 静音warning snprintf 拼接 workdir + "/" + rel_path 时，编译器理论上算出两个字符串加起来可能超过4096字节会截断。但实际使用中路径根本不会那么长，snprintf 截断也是安全行为
# 还是不修改TA的sandbox比较好的
LDLIBS  := -lpthread -lm

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer

# Source partitioning ---------------------------------------------------------

AGENT_SRCS := $(sort $(wildcard agent/*.c))
TOOL_SRCS  := $(sort $(wildcard tools/*.c))
UI_SRCS    := $(sort $(wildcard ui/*.c))
CTX_SRCS   := $(sort $(wildcard context/*.c))

CORE_SRCS  := main.c cmd.c session.c memory.c skills.c config.c message.c util.c http.c hooks.c \
              $(AGENT_SRCS) $(TOOL_SRCS) $(UI_SRCS) $(CTX_SRCS)

SRCS := $(CORE_SRCS) $(EXTRA_SRCS)

BUILD     := build
OBJS      := $(SRCS:%.c=$(BUILD)/%.o) $(BUILD)/cJSON.o $(BUILD)/md4c.o
DEPS      := $(SRCS:%.c=$(BUILD)/%.d) $(BUILD)/cJSON.d $(BUILD)/md4c.d
TARGET    := $(BUILD)/c-agent
ASAN_TGT  := $(BUILD)/c-agent-asan
TSAN_TGT  := $(BUILD)/c-agent-tsan

.PHONY: all clean clean-objs \
        test test-a test-b test-ca test-cb \
        test-asan test-tsan test-agent-tsan \
        test-cunit test-cunit-parallel test-cunit-tsan \
        test-cunit-offload test-cunit-summary \
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

$(BUILD)/md4c.o: libs/md4c.c
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

# ── Integration tests via the mock LLM server ───────────────────────────

test: all
	python3 tests/run_tests.py --bin ./$(TARGET)

test-a: all
	python3 tests/run_tests.py --bin ./$(TARGET) --phase a

test-b: all
	python3 tests/run_tests.py --bin ./$(TARGET) --phase b

test-ca: all
	python3 tests/run_tests.py --bin ./$(TARGET) --phase ca

test-cb: all
	python3 tests/run_tests.py --bin ./$(TARGET) --phase cb

test-asan: asan
	python3 tests/run_tests.py --bin ./$(ASAN_TGT)

test-tsan: test-cunit-tsan

test-agent-tsan: tsan
	python3 tests/run_tests.py --bin ./$(TSAN_TGT) --phase b

# ── Pure-C unit tests ───────────────────────────────────────────────────

CUNIT_CORE  := config.c message.c util.c
CUNIT_TOOLS := tools/registry.c tools/sandbox.c \
               tools/bash.c tools/read.c tools/write.c tools/edit.c
CUNIT_CTX_BASE := context/context.c context/token.c

define cunit_link_simple
$(CC) $(CFLAGS) $(1) tests/cunit/$(2).c \
    $(CUNIT_CORE) $(CUNIT_TOOLS) \
    libs/cJSON.c \
    -o $(BUILD)/cunit-$(3)/$(2) $(LDLIBS)
endef

define cunit_link_executor
$(CC) $(CFLAGS) $(1) tests/cunit/test_executor.c \
    $(CUNIT_CORE) $(CUNIT_TOOLS) tools/executor.c \
    ui/ui.c ui/render.c \
    libs/cJSON.c $(EXTRA_SRCS) \
    -o $(BUILD)/cunit-$(2)/test_executor $(LDLIBS)
endef

define cunit_link_offload
$(CC) $(CFLAGS) $(1) tests/cunit/test_offload.c \
    $(CUNIT_CORE) $(CUNIT_CTX_BASE) context/policy_offload.c \
    libs/cJSON.c \
    -o $(BUILD)/cunit-$(2)/test_offload $(LDLIBS)
endef

define cunit_link_summary
$(CC) $(CFLAGS) $(1) tests/cunit/test_summary.c tests/cunit/fake_summarizer.c \
    $(CUNIT_CORE) $(CUNIT_CTX_BASE) context/policy_summary.c \
    libs/cJSON.c \
    -o $(BUILD)/cunit-$(2)/test_summary $(LDLIBS)
endef

test-cunit:
	@mkdir -p $(BUILD)/cunit-asan
	@for bn in test_registry test_sandbox; do \
	  echo "  CC   $$bn (asan)"; \
	  $(call cunit_link_simple,$(ASAN_FLAGS),$$bn,asan) || exit 1; \
	  $(BUILD)/cunit-asan/$$bn || exit 1; \
	done
	@echo "  CC   test_offload (asan)"
	@$(call cunit_link_offload,$(ASAN_FLAGS),asan) && $(BUILD)/cunit-asan/test_offload
	@echo "  CC   test_summary (asan)"
	@$(call cunit_link_summary,$(ASAN_FLAGS),asan) && $(BUILD)/cunit-asan/test_summary

test-cunit-offload:
	@mkdir -p $(BUILD)/cunit-asan
	@echo "  CC   test_offload (asan)"
	@$(call cunit_link_offload,$(ASAN_FLAGS),asan)
	@$(BUILD)/cunit-asan/test_offload

test-cunit-summary:
	@mkdir -p $(BUILD)/cunit-asan
	@echo "  CC   test_summary (asan)"
	@$(call cunit_link_summary,$(ASAN_FLAGS),asan)
	@$(BUILD)/cunit-asan/test_summary

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
