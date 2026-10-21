// Forced into every compile of a build.sh profile by hardening.py (ADR 5).
// libc++ ends a hardening trap in abort(), which on macOS leaves a crash
// report and a CrashReporter dialog; this definition of its abort hook
// prints the trap's message and exits 1 instead, so a trap in a unit test
// fails the test run like any other failure.  libstdc++ (the container)
// does not read the macro and aborts, which leaves no report there.
#pragma once
#if defined(__cplusplus) && defined(__APPLE__)
// The two libc functions, declared here so that no header is forced into
// every file.
extern "C" int vdprintf(int, const char *, __builtin_va_list);
extern "C" [[noreturn]]
void _exit(int); // NOLINT(bugprone-reserved-identifier,readability-identifier-naming): libc's name
[[noreturn]]
inline void dxx_hardening_exit(const char *format, ...) noexcept
{
	__builtin_va_list ap;
	__builtin_va_start(ap, format);
	// NOLINTNEXTLINE(clang-diagnostic-unsafe-buffer-usage-in-libc-call): libc++'s format
	vdprintf(2, format, ap);
	__builtin_va_end(ap);
	_exit(1);
}
// NOLINTNEXTLINE(bugprone-reserved-identifier): libc++'s documented hook
#define _LIBCPP_VERBOSE_ABORT(...) ::dxx_hardening_exit(__VA_ARGS__)
#endif
