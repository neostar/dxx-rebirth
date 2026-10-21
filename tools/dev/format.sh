#!/usr/bin/env bash
# Format or check fork code; upstream files are never touched.
#
#   format.sh [--check] [DIR...]   clang-format (--check: report, change nothing)
#   format.sh --tidy [DIR...]      clang-tidy with build/dev/compile_commands.json
#   format.sh --staged             the pre-commit gate: --check and --tidy on
#                                  the staged C/C++ files in fork directories
#
# DIRs default to the directories listed in tools/dev/fork-dirs.  --tidy and
# --staged also fail on a NOLINT without a check list and a reason (the C++
# safety rules ADR: `NOLINT(check): reason`) and on a fork directory whose
# .clang-tidy is not a copy of tools/dev/style/clang-tidy, which clangd and
# clang-tidy read from there.  --staged checks the staged content only: the
# staged files are checked out of the index into a temporary tree, and
# NOLINT and clang-tidy read them there, with the compilation database's
# flags for the real path; C files get clang-format only.  A file missing
# from the compilation database gets the flags of its nearest neighbour.  Exit
# status 1 means a file needs formatting or clang-tidy reported a problem, 2
# a usage error or a missing or empty fork-dirs.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"
clang_format=$(dev_llvm_tool clang-format)
clang_tidy=$(dev_llvm_tool clang-tidy)
tidy_config=$dev_dir/style/clang-tidy
database=$root/build/dev

mode=format
case "${1-}" in
	--check) mode=check; shift ;;
	--tidy) mode=tidy; shift ;;
	--staged) mode=staged; shift ;;
	-*) echo "usage: $0 [--check|--tidy|--staged] [DIR...]" >&2; exit 2 ;;
esac

fork_dirs=()
if [[ -f $dev_dir/fork-dirs ]]; then
	while IFS= read -r d; do
		[[ -z $d || $d == \#* ]] || fork_dirs+=("$d")
	done < "$dev_dir/fork-dirs"
fi
if [[ ${#fork_dirs[@]} -eq 0 && ($# -eq 0 || $mode == staged) ]]; then
	echo "format.sh: $dev_dir/fork-dirs is missing or lists no directory" >&2
	exit 2
fi

# The LLVM release finds the macOS SDK through SDKROOT.
if [[ $dev_os == darwin && -z ${SDKROOT-} ]]; then
	SDKROOT=$(xcrun --show-sdk-path 2>/dev/null) && export SDKROOT
fi

files=()
if [[ $mode == staged ]]; then
	while IFS= read -r -d '' f; do
		files+=("$root/$f")
	done < <(git -C "$root" diff --cached --name-only --diff-filter=ACMR -z -- \
		"${fork_dirs[@]/%//*.cpp}" "${fork_dirs[@]/%//*.h}" "${fork_dirs[@]/%//*.c}")
else
	dirs=("$@")
	[[ ${#dirs[@]} -eq 0 ]] && dirs=("${fork_dirs[@]/#/$root/}")
	while IFS= read -r -d '' f; do
		files+=("$f")
	done < <(find "${dirs[@]}" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) -print0)
fi

# Fail unless every fork directory holds a current copy of the tidy config.
check_tidy_configs()
{
	local d rc=0
	for d in "${fork_dirs[@]}"; do
		cmp -s "$tidy_config" "$root/$d/.clang-tidy" && continue
		echo "$d/.clang-tidy: not a copy of tools/dev/style/clang-tidy;" \
			"run cp tools/dev/style/clang-tidy $d/.clang-tidy"
		rc=1
	done
	return "$rc"
}

# --staged: the temporary tree holding the staged files, whose paths are
# reported as the repository's, and clang-tidy's options for it: the tree
# has no .clang-tidy, but every fork directory's is a copy of tidy_config.
tree=
tidy_opts=()
# Print path $1 relative to the repository (or the staged tree).
rel()
{
	local f=${1#"$root"/}
	[[ -n $tree ]] && f=${f#"$tree"/}
	printf '%s\n' "$f"
}

# Fail on each NOLINT in files "$@" that lacks a check list and a reason.
check_nolint()
{
	local f n line rc=0
	for f in "$@"; do
		n=0
		while IFS= read -r line; do
			n=$((n + 1))
			[[ $line == *NOLINT* ]] || continue
			[[ $line =~ NOLINT(NEXTLINE|BEGIN)?\([^\)]+\):\ *[^\ ] ||
				$line =~ NOLINTEND\([^\)]+\) ]] && continue
			echo "$(rel "$f"):$n: NOLINT without a check and a reason;" \
				"write NOLINT(check): reason"
			rc=1
		done < "$f"
	done
	return "$rc"
}

# clang-tidy the C++ files among "$@" with the compilation database; the
# clang-tidy rules are C++ rules, so C files are left out.  A header has
# no entry of its own, and the one clang-tidy would borrow may be GCC's
# (the container), with flags clang rejects; headers are tidied with the
# fork's own flags instead (fork_warnings.py, the pinned dependencies).
tidy()
{
	local f rc=0 cxx=() headers=() flags=()
	for f in "$@"; do
		case $f in
		*.c) ;;
		*.h|*.hh|*.hpp) headers+=("$f") ;;
		*) cxx+=("$f") ;;
		esac
	done
	if [[ ${#headers[@]} -gt 0 ]]; then
		read -ra flags <<<"$(python3 "$dev_dir/fork_warnings.py")"
		[[ $dev_os == darwin ]] && flags+=(-isysroot "$(xcrun --show-sdk-path)")
		# A staged header's own directory, for the includes beside it.
		for f in "${headers[@]}"; do
			flags+=(-I "$root/$(dirname "$(rel "$f")")")
		done
		"$clang_tidy" "${tidy_opts[@]+"${tidy_opts[@]}"}" --quiet "${headers[@]}" -- -x c++-header -std=c++23 "${flags[@]}" \
			-isystem "$dev_deps_prefix/include" 2>&1 | strip_tree
		[[ ${PIPESTATUS[0]} -eq 0 ]] || rc=1
	fi
	[[ ${#cxx[@]} -eq 0 ]] && return "$rc"
	if [[ ! -f $database/compile_commands.json ]]; then
		echo "format.sh: no $database/compile_commands.json; run tools/dev/build.sh first" >&2
		return 1
	fi
	local db=$database
	if [[ -n $tree ]]; then
		db=$tree/.database
		mkdir -p "$db"
		staged_database "$database/compile_commands.json" "${cxx[@]}" \
			> "$db/compile_commands.json" || return 1
	fi
	"$clang_tidy" "${tidy_opts[@]+"${tidy_opts[@]}"}" -p "$db" --quiet "${cxx[@]}" 2>&1 | strip_tree
	[[ ${PIPESTATUS[0]} -eq 0 && $rc -eq 0 ]]
}

# Filter clang-tidy's output: drop its warning counts and name the staged
# tree's files by their repository paths.
strip_tree()
{
	grep -v '^[0-9]* warnings\? generated\.$' | if [[ -n $tree ]]; then
		sed "s|$tree/||g"
	else
		cat
	fi
}

# Print a compilation database for the staged copies "${@:2}" (in $tree)
# from database $1: each gets the entry of its real path, or of its
# nearest neighbour, with the source swapped for the copy and the real
# directory added to the include path.
staged_database()
{
	python3 - "$1" "$root" "$tree" "${@:2}" <<'PY'
import json, os, shlex, sys
db, root, tree, copies = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
entries = json.load(open(db))
def path(e):
    return os.path.normpath(os.path.join(e["directory"], e["file"]))
out = []
for copy in copies:
    real = os.path.join(root, os.path.relpath(copy, tree))
    e = min(entries, key=lambda e: (path(e) != real,
                                    -len(os.path.commonpath([path(e), real]))))
    args = e.get("arguments") or shlex.split(e["command"])
    args = [copy if os.path.normpath(os.path.join(e["directory"], a)) == path(e)
            else a for a in args]
    args[1:1] = ["-I", os.path.dirname(real)]
    out.append({"directory": e["directory"], "file": copy, "arguments": args})
json.dump(out, sys.stdout, indent=1)
PY
}

style="--style=file:$dev_dir/style/clang-format"
case $mode in
format)
	[[ ${#files[@]} -eq 0 ]] && exit 0
	exec "$clang_format" "$style" -i "${files[@]}" ;;
check)
	[[ ${#files[@]} -eq 0 ]] && exit 0
	"$clang_format" "$style" --dry-run --Werror "${files[@]}" || exit 1 ;;
tidy|staged)
	rc=0
	check_tidy_configs || rc=1
	[[ ${#files[@]} -eq 0 ]] && exit "$rc"
	if [[ $mode == staged ]]; then
		tree=$(mktemp -d "${TMPDIR:-/tmp}/format-staged.XXXXXX") || exit 1
		trap 'rm -rf "$tree"' EXIT
		tidy_opts=(--config-file="$tidy_config")
		staged=("${files[@]#"$root"/}")
		git -C "$root" checkout-index --prefix="$tree/" -- "${staged[@]}" || exit 1
		files=("${staged[@]/#/$tree/}")
		for f in "${files[@]}"; do
			"$clang_format" "$style" --assume-filename="$(rel "$f")" --dry-run --Werror < "$f" ||
				{ echo "$(rel "$f"): not formatted; run tools/dev/format.sh"; rc=1; }
		done
	fi
	check_nolint "${files[@]}" || rc=1
	tidy "${files[@]}" || rc=1
	exit "$rc" ;;
esac
