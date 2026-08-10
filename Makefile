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

.PHONY: format format-check tools python-tools node-tools test clean

tools: python-tools node-tools

# Reinstall when the pins change, and also when the installed tools do not
# actually run. Testing that they run matters more than it looks: a .venv is not
# relocatable or portable, so one left behind by an upgraded Python, copied
# between machines, or written by a container sharing this directory still has a
# .venv/bin/clang-format sitting there in the right place. It just imports
# nothing, because site-packages is under the old interpreter's version number.
# A rule that only asked whether that file existed would skip the install and
# hand the failure to the user as a Python traceback.
python-tools:
	@if ! $(CLANG_FORMAT) --version >/dev/null 2>&1 \
	   || ! $(RUFF) --version >/dev/null 2>&1 \
	   || [ requirements-dev.txt -nt $(VENV)/pyvenv.cfg ]; then \
	    echo "Installing pinned Python tools into $(VENV)"; \
	    rm -rf $(VENV); \
	    python3 -m venv $(VENV); \
	    $(VENV)/bin/pip install --quiet --upgrade pip; \
	    $(VENV)/bin/pip install --quiet -r requirements-dev.txt; \
	fi

# Same reasoning, though node_modules survives moving between machines far
# better: Prettier is pure JavaScript with nothing compiled per platform.
node-tools:
	@if ! $(PRETTIER) --version >/dev/null 2>&1 \
	   || [ package-lock.json -nt node_modules/.package-lock.json ]; then \
	    echo "Installing pinned Node tools into node_modules"; \
	    npm ci --silent; \
	fi

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
