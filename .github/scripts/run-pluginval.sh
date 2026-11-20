#!/usr/bin/env bash
# Runs pluginval against the VST3 at $1, from the folder fetch-pluginval.sh
# unpacked it into.
#
# pluginval is a host that loads the plugin and misuses it on purpose: odd
# block sizes, parameters set from the wrong thread, state saved and restored
# mid-render, buses enabled that nothing asked for, and the editor opened and
# closed. None of that is reachable from the JUCE-linked tests, which drive the
# processor directly rather than through a plugin format.
set -euo pipefail

vst3="$1"

# A pinned seed on a push or a pull request, so the same code always gets the
# same answer, and a seed nobody chose on the weekly run, so the rest of the
# space is covered too. pluginval reads 0 as "pick one" and prints the seed it
# used either way, so a weekly failure can be run again exactly.
if [ "${GITHUB_EVENT_NAME:-}" = "schedule" ]; then
  seed=0
else
  seed=0x63cc70f
fi
echo "random seed: $seed"

args=(--strictness-level 8 --random-seed "$seed" --validate "$vst3")

case "$RUNNER_OS" in
  macOS)
    ./pluginval.app/Contents/MacOS/pluginval "${args[@]}"
    ;;
  Windows)
    ./pluginval.exe "${args[@]}"
    ;;
  Linux)
    # Under a virtual display, since the editor tests open the window, and
    # through gdb, since a crash otherwise says only "exit code 139". gdb
    # prints where every thread was and still returns the plugin's own exit
    # code.
    #
    # A crash after SUCCESS is let through with a warning. Overtonium's CI has
    # seen pluginval pass every test and then die in its own shutdown, in
    # thread-local storage, the run loop or XCloseDisplay, and a re-run was
    # green each time. A failed test, or a crash before SUCCESS, still fails.
    set +e
    xvfb-run -a gdb --batch --return-child-result \
      -ex "handle SIGPIPE nostop noprint pass" \
      -ex run \
      -ex "echo \n==== the crash, every thread ====\n" \
      -ex "thread apply all bt" \
      --args ./pluginval "${args[@]}" 2>&1 | tee pluginval.log
    status=${PIPESTATUS[0]}
    set -e

    # pluginval prints SUCCESS on a line of its own once every test has
    # passed. Its failures read "!!! Test", "*** FAILED", "FAILURE" or "tests
    # failed". Its warnings also start with "!!!", so that alone is not taken
    # as a failure.
    if [ "$status" -ne 0 ] && grep -qx "SUCCESS" pluginval.log &&
      ! grep -qE '^!!! Test |\*\*\* FAILED|^FAILURE|tests failed' pluginval.log; then
      echo "::warning::pluginval passed every test and then exited with $status while shutting down. See the stack above."
      exit 0
    fi
    exit "$status"
    ;;
esac
