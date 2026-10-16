# Source lists are explicit; physical modules are added only after approval.
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
SHELL := sh

include mk/app.mk
include mk/component.mk
include mk/solver.mk
include mk/tools.mk

CC := clang
CPPFLAGS := -Iinclude
CFLAGS_COMMON := -std=c11 -Wall -Wextra -Wshadow -MMD -MP
EXTRA_CFLAGS ?=
CFLAGS := $(CFLAGS_COMMON) -O3 -march=native $(EXTRA_CFLAGS)
CFLAGS_DEBUG := $(CFLAGS_COMMON) -O0 -g3 $(EXTRA_CFLAGS)
LDFLAGS :=
LDLIBS := -lm

# Use actual Windows filenames so a second make does not relink every time.
EXEEXT := $(if $(filter Windows_NT,$(OS)),.exe,)
BIN := build/main$(EXEEXT)
BIN_DEBUG := build/main_debug$(EXEEXT)
BIN_CHAR := build/char$(EXEEXT)
BIN_CHAR_DEBUG := build/char_debug$(EXEEXT)
SHARED_SRCS := $(shared_SRCS) $(component_SRCS) $(solver_SRCS)
SRCS := $(app_SRCS) $(SHARED_SRCS)
OBJS := $(SRCS:%.c=build/%.o)
DEBUG_OBJS := $(SRCS:%.c=build/debug/%.o)
CHAR_OBJS := $(tools_SRCS:%.c=build/tools/%.o) $(SHARED_SRCS:%.c=build/%.o)
CHAR_DEBUG_OBJS := $(tools_SRCS:%.c=build/debug/%.o) $(SHARED_SRCS:%.c=build/debug/%.o)
BUILD_FILES := Makefile $(wildcard mk/*.mk)

-include $(OBJS:.o=.d) $(DEBUG_OBJS:.o=.d) $(CHAR_OBJS:.o=.d) $(CHAR_DEBUG_OBJS:.o=.d)

build/%.o: %.c $(BUILD_FILES)
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/debug/%.o: %.c $(BUILD_FILES)
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS_DEBUG) -c $< -o $@

build/tools/%.o: %.c $(BUILD_FILES)
	@mkdir -p "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BIN_DEBUG): $(DEBUG_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BIN_CHAR): $(CHAR_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BIN_CHAR_DEBUG): $(CHAR_DEBUG_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Workspace venv first, project-local venv second, then PATH python/python3.
VENV_PYTHON := $(firstword $(wildcard ../.venv/Scripts/python.exe ../.venv/bin/python .venv/Scripts/python.exe .venv/bin/python))
PYTHON ?= $(if $(VENV_PYTHON),$(VENV_PYTHON),$(shell command -v python >/dev/null 2>&1 && echo python || echo python3))
PLOT_ARGS ?=
PLOT_SCRIPT ?= plotting/plot.py
ARGS ?=

.PHONY: all debug run char check plot run-plot clean help results-dir
all: $(BIN) $(BIN_CHAR)
debug: $(BIN_DEBUG) $(BIN_CHAR_DEBUG)
char: $(BIN_CHAR)

results-dir:
	@mkdir -p results

run: $(BIN) | results-dir
	./$(BIN) $(ARGS)

check: $(BIN_CHAR) | results-dir
	./$(BIN_CHAR) check

plot:
	"$(PYTHON)" $(PLOT_SCRIPT) $(PLOT_ARGS)

# Recipe recursion guarantees run finishes before plot, even under make -j.
run-plot: run
	$(MAKE) plot

ifeq ($(OS),Windows_NT)
clean:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/clean.ps1
else
clean:
	@test ! -L build || { echo "Refusing to clean a symlink: build" >&2; exit 1; }
	rm -rf -- build
endif

help:
	@echo "ATRcore S05 (fixed LOX GasGenerator ELM and component checks)"
	@echo "  all       Build release main and C check entry"
	@echo "  debug     Build main_debug and char_debug with -O0 -g3"
	@echo "  run       Inspect boundary; ARGS='--help' lists CLI options"
	@echo "  char      Build build/char$(EXEEXT)"
	@echo "  check     Run C checks including GasGenerator layer-by-layer comparison"
	@echo "  plot      Use PLOT_SCRIPT=plotting/plot_char.py for component CSV"
	@echo "  run-plot  Run, then plot, sequentially"
	@echo "  clean     Remove build only; keep results and reports"
	@echo "  help      Show this help"

