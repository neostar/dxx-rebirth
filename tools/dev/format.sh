#!/usr/bin/env bash
# Format or check fork code; upstream files are never touched.
#
#   format.sh [--check] [DIR...]   clang-format (--check: report, change nothing)
#   format.sh --tidy [DIR...]      clang-tidy with build/dev/compile_commands.json
#
# DIRs default to the directories listed in tools/dev/fork-dirs.  Exit status
# 1 means a file needs formatting or clang-tidy reported a problem, 2 a
# usage error or a missing or empty fork-dirs.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"
clang_format=$(dev_llvm_tool clang-format)
clang_tidy=$(dev_llvm_tool clang-tidy)

mode=format
case "${1-}" in
	--check) mode=check; shift ;;
	--tidy) mode=tidy; shift ;;
	-*) echo "usage: $0 [--check|--tidy] [DIR...]" >&2; exit 2 ;;
esac

dirs=("$@")
if [[ ${#dirs[@]} -eq 0 ]]; then
	if [[ -f $dev_dir/fork-dirs ]]; then
		while IFS= read -r d; do
			[[ -z $d || $d == \#* ]] || dirs+=("$root/$d")
		done < "$dev_dir/fork-dirs"
	fi
	if [[ ${#dirs[@]} -eq 0 ]]; then
		echo "format.sh: $dev_dir/fork-dirs is missing or lists no directory" >&2
		exit 2
	fi
fi

files=()
while IFS= read -r -d '' f; do
	files+=("$f")
done < <(find "${dirs[@]}" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) -print0)
[[ ${#files[@]} -eq 0 ]] && exit 0

style="--style=file:$dev_dir/style/clang-format"
case $mode in
	format) exec "$clang_format" "$style" -i "${files[@]}" ;;
	check) "$clang_format" "$style" --dry-run --Werror "${files[@]}" || exit 1 ;;
	tidy) "$clang_tidy" --config-file="$dev_dir/style/clang-tidy" -p "$root/build/dev" \
		--quiet "${files[@]}" || exit 1 ;;
esac
