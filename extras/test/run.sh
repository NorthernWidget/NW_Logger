#!/bin/sh
# Output-regression test for NW_Logger's timestamp parse: compiles
# src/NW_TimeStamp.h against the NW_Core stubs. The rest of the logger needs a
# card, a clock and a board, and lives in NW-Sim's transcripts instead.
# Usage: ./run.sh [--record]
cd "$(dirname "$0")" || exit 1
exec ../../../NW_Core/extras/test/run_library.sh nw_logger_test "$@"
