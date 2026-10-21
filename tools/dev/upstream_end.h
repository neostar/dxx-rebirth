// Closes the region upstream_begin.h opens; without one open, it fails,
// except when it is the main file (clang-tidy on this header alone).
#if !defined(DXX_UPSTREAM_REGION) && __INCLUDE_LEVEL__ > 0
#error "upstream_end.h without upstream_begin.h"
#endif
#undef DXX_UPSTREAM_REGION
// NOLINTNEXTLINE(clang-diagnostic-unknown-pragmas): the push is in upstream_begin.h
#pragma GCC diagnostic pop
