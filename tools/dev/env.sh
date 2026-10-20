# Environment shared by the tools in tools/dev and their tests; sourced,
# not run.

# git's repository variables.  Inside a git hook git exports some of them
# for the repository being committed to; the shell tests must never see
# any of them, or their throwaway repositories become that repository.
dev_git_repo_vars=(GIT_DIR GIT_INDEX_FILE GIT_WORK_TREE GIT_COMMON_DIR
	GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_PREFIX
	GIT_NAMESPACE)

# Sanitizer failures exit non-zero instead of calling abort(): on macOS an
# abort brings up a CrashReporter dialog.  LeakSanitizer is unsupported on
# Apple Silicon macOS (use `leaks --atExit` there); elsewhere it stays on.
# Settings already in the environment win.
if [[ $(uname -s) == Darwin ]]; then
	_dev_detect_leaks=0
else
	_dev_detect_leaks=1
fi
export ASAN_OPTIONS=${ASAN_OPTIONS:-abort_on_error=0:halt_on_error=1:detect_leaks=$_dev_detect_leaks}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:abort_on_error=0}
unset _dev_detect_leaks

# Print the path of LLVM tool $1 (clang-format, clang-tidy): from PATH, else
# from Homebrew's keg-only llvm.
dev_llvm_tool()
{
	command -v "$1" || echo "$(brew --prefix llvm 2>/dev/null)/bin/$1"
}
