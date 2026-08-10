# Repo-level developer tasks. The actual builds live elsewhere: core/Makefile
# builds the framework-agnostic core tests, and CMakeLists.txt builds the plugin.
#
#   make format         rewrite every tracked source file in the house style
#   make format-check    fail if anything is unformatted or fails lint (CI runs this)
#   make tools           install the pinned formatters, without running them
#   make test            run the core protocol tests
#
# Three formatters, one entry point, because the repo is three languages:
# clang-format for the C++ (.clang-format), Prettier for the docs site, Markdown
# and workflow YAML (.prettierrc), and Ruff for the Python tools (ruff.toml).
#
# Every version is pinned, in package-lock.json and requirements-dev.txt, so that
# a local run and a CI run produce byte-identical output. An unpinned formatter
# means format-check fails on a machine that did nothing wrong.

VENV := .venv
CLANG_FORMAT := $(VENV)/bin/clang-format
RUFF := $(VENV)/bin/ruff
PRETTIER := npx --no-install prettier

# Tracked files only, so build output, the vendored JUCE checkout and anything
# untracked are all excluded without needing a second ignore list to maintain.
# Deleted-but-not-yet-committed files are filtered out by the wildcard.
CPP_FILES = $(wildcard $(shell git ls-files '*.cpp' '*.h'))

.PHONY: format format-check tools test clean

tools: $(CLANG_FORMAT) node_modules/.package-lock.json

# One stamp file per toolchain, so the installs only rerun when the pins change.
$(CLANG_FORMAT) $(RUFF): requirements-dev.txt
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --quiet --upgrade pip
	$(VENV)/bin/pip install --quiet -r requirements-dev.txt
	@touch $(CLANG_FORMAT) $(RUFF)

node_modules/.package-lock.json: package-lock.json package.json
	npm ci --silent

format: tools
	$(CLANG_FORMAT) -i $(CPP_FILES)
	$(PRETTIER) --write --log-level warn .
	$(RUFF) format .
	$(RUFF) check --fix .

# -Werror turns clang-format's "would change this file" note into a failure;
# without it --dry-run reports and still exits 0.
format-check: tools
	$(CLANG_FORMAT) --dry-run -Werror $(CPP_FILES)
	$(PRETTIER) --check --log-level warn .
	$(RUFF) format --check .
	$(RUFF) check .

test:
	$(MAKE) -C core test

clean:
	$(MAKE) -C core clean
	rm -rf $(VENV) node_modules
