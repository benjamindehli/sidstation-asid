#!/usr/bin/env bash
# The X11, ALSA, JACK, FreeType and GL headers JUCE needs to build on Linux.
# Shared by the CI plugin build and the release tarball build, so the two cannot
# drift into installing different sets.
#
# Written as a retry loop because plain `apt-get update` is not reliable on the
# hosted runners. The Azure mirror is often unreachable (four `Ign` lines, then a
# fallback to archive.ubuntu.com) and the fallback sometimes stalls mid-fetch
# rather than failing. A stall is the bad case: apt sits there, the step never
# fails, and the job runs to the workflow timeout. So every attempt gets its own
# wall clock from `timeout`, which turns a stall into a failed attempt this loop
# can retry on a hopefully healthier mirror.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

PACKAGES=(
    libasound2-dev libjack-jackd2-dev
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev
    libxinerama-dev libxrandr-dev libxrender-dev
    libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev
)

# Acquire::Retries makes apt itself retry a failed mirror before giving up, and
# the two Timeout options bound a single connection: without them apt's default
# is patient enough that the outer `timeout` is the only thing that ever fires.
apt_retry() {
    local what="$1" attempt
    for attempt in 1 2 3; do
        # 180 s three times over, plus the backoff, is 9.5 min: comfortably
        # inside the 10 min step timeout in the workflows, so a wedged mirror
        # ends in the error message below rather than a killed step.
        if timeout 180 sudo -E apt-get \
            -o Acquire::Retries=3 \
            -o Acquire::http::Timeout=20 \
            -o Acquire::https::Timeout=20 \
            "$@"; then
            return 0
        fi
        echo "::warning::apt-get $what failed or timed out (attempt $attempt of 3)"
        [ "$attempt" -lt 3 ] && sleep 15
    done
    echo "::error::apt-get $what did not succeed after 3 attempts"
    return 1
}

apt_retry update
apt_retry install -y "${PACKAGES[@]}"
