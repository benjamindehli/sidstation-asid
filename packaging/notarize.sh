#!/usr/bin/env bash
# Submit a file (.pkg, .dmg or .zip) to Apple's notary service and wait for the
# verdict. The caller staples the ticket afterwards (xcrun stapler staple).
#
# Needs these in the environment:
#   APPLE_ID            Apple-ID email
#   APPLE_TEAM_ID       10-character team id
#   APPLE_APP_PASSWORD  app-specific password (appleid.apple.com)
set -euo pipefail

file="${1:?usage: notarize.sh <file>}"

xcrun notarytool submit "$file" \
    --apple-id "$APPLE_ID" \
    --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" \
    --wait
