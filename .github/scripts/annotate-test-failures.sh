#!/usr/bin/env bash
# Turns the output of failed CTest tests into GitHub error annotations, so failures are readable
# on the run page and through the public API without downloading logs.
#
#   annotate-test-failures.sh <ctest log file>
#
# Expects the log of `ctest --output-on-failure`. Emits one annotation per failed test (at most
# 8, each truncated to 80 lines).

set -euo pipefail

log="${1:?usage: annotate-test-failures.sh <ctest log>}"
[ -f "$log" ] || { echo "::error title=ctest::no log at $log"; exit 0; }

awk '
    function flush() {
        if (name != "" && count < 8) {
            gsub(/%/, "%25", body); gsub(/\r/, "%0D", body); gsub(/\n/, "%0A", body)
            printf "::error title=Test failed: %s::%s\n", name, body
            count++
        }
        name = ""; body = ""; lines = 0
    }
    /Test +#[0-9]+: .*(\*\*\*|Failed|Exception|Timeout)/ && !/Passed|Skipped|Not Run/ {
        flush()
        name = $0
        sub(/^.*Test +#[0-9]+: /, "", name)
        sub(/ \.+.*$/, "", name)
        gsub(/%/, "%25", name); gsub(/:/, "%3A", name); gsub(/,/, "%2C", name)
        next
    }
    /^ *Start +[0-9]+: / || /tests passed, / || /% tests passed/ { flush(); next }
    name != "" && lines < 80 { body = body $0 "\n"; lines++ }
    END { flush() }
' "$log"

grep -A20 "The following tests FAILED" "$log" | sed 's/^/::error title=ctest summary::/' | head -20 || true
