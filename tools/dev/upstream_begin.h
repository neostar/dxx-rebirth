// Fork code includes an upstream header that the strict warnings
// (fork_warnings.py) trip on, such as timer.h, between this file and
// upstream_end.h, with clang-format off around the three lines so that
// include sorting keeps them together (ADR 5):
//
//   // clang-format off
//   #include "tools/dev/upstream_begin.h"
//   #include "timer.h"
//   #include "tools/dev/upstream_end.h"
//   // clang-format on
//
// The strict warnings stay on where fork code expands the header's macros,
// whose diagnostics GCC and clang place at the expansion; they are lost in
// the header's own code, templates instantiated from fork code among them.
// No pragma once: each use opens a new region.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
