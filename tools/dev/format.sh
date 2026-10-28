#!/usr/bin/env bash
# Format or check fork code; upstream files are never touched.
#
#   format.sh [--check] [DIR...]   clang-format (--check: report, change nothing)
#   format.sh --tidy [DIR...]      clang-tidy with build/dev/compile_commands.json
#   format.sh --staged             the pre-commit gate: --check and --tidy on
#                                  the staged C, C++ and Objective-C(++) files
#                                  in fork directories (.c .cc .cpp .cxx .h
#                                  .hh .hpp .hxx .m .mm)
#
# DIRs default to the directories listed in tools/dev/fork-dirs.  --tidy and
# --staged also fail on a NOLINT without a check list and a reason (the C++
# safety rules ADR: `NOLINT(check): reason`) and on a fork directory whose
# .clang-tidy is not a copy of tools/dev/style/clang-tidy, which clangd and
# clang-tidy read from there.  --staged checks the staged content only: the
# staged files are checked out of the index into a temporary tree, and
# NOLINT and clang-tidy read them there, with the compilation database's
# flags for the real path; C files get clang-format only.  A staged file
# missing from the compilation database gets the flags of its nearest
# neighbour; --tidy fails on a C++ source file missing from it, naming the
# file (clang-tidy would borrow a neighbour's command and lose ExtraArgs).
# An SDL3-only source (tools/dev/sdl3-only: built only with sdl3=1, so
# missing from build/dev's database) is tidied with build/dev-sdl3's
# database instead, failing the same way when that lacks it; without that
# database it is a SKIP naming the file and the build that writes it.
#
# clang-tidy reports findings in fork headers only (--header-filter built
# from fork-dirs), none in upstream or third-party ones; a header named
# through `..` may lie outside the fork directory it starts in and is left
# out, so --tidy and --staged fail on a fork file whose #include "..."
# climbs through `..`.  Each unittest/ directory in a fork directory holds a
# copy of tools/dev/style/clang-tidy-unittest, checked like the fork
# directory's, and any other .clang-tidy under a fork directory fails;
# --staged reads the configs from the index (an untracked one from the
# working tree).  --tidy and --staged also fail on an include through `..`
# and on an unpaired upstream_begin.h or upstream_end.h (check_includes).  Source
# files keep their database entry, less the flags the pinned clang-tidy
# rejects (GCC's, in the container), found by one probe run and dropped with
# --removed-arg.  Files are tidied in one clang-tidy run each, env.sh's
# dev_jobs (DXX_JOBS) at a time, the largest first; each file's output prints
# whole in the order given, and any run failing fails --tidy.  A run per file
# also reads each file's own .clang-tidy: clang-tidy filters every file's
# findings in a run by the config of the last file, so a unittest/ file
# would otherwise switch off checks for the others.  Exit status 1 means a file needs formatting or clang-tidy
# reported a problem, 2 a usage error or a missing or empty fork-dirs.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"
clang_format=$(dev_llvm_tool clang-format)
clang_tidy=$(dev_llvm_tool clang-tidy)
tidy_config=$dev_dir/style/clang-tidy
unittest_config=$dev_dir/style/clang-tidy-unittest
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

# The fork's C, C++ and Objective-C(++) sources and headers, as pathspecs
# for --staged and as find tests otherwise.
exts=(c cc cpp cxx h hh hpp hxx m mm)
specs=() names=()
for e in "${exts[@]}"; do
	specs+=("${fork_dirs[@]/%//*.$e}")
	[[ ${#names[@]} -gt 0 ]] && names+=(-o)
	names+=(-name "*.$e")
done

files=()
if [[ $mode == staged ]]; then
	while IFS= read -r -d '' f; do
		files+=("$root/$f")
	done < <(git -C "$root" diff --cached --name-only --diff-filter=ACMR -z -- "${specs[@]}")
else
	dirs=("$@")
	[[ ${#dirs[@]} -eq 0 ]] && dirs=("${fork_dirs[@]/#/$root/}")
	while IFS= read -r -d '' f; do
		files+=("$f")
	done < <(find "${dirs[@]}" -type f \( "${names[@]}" \) -print0)
fi

# Fail unless every fork directory under base directory $1 (the repository,
# or the staged tree, which holds the index's configs) holds a current copy
# of the tidy config, and every unittest/ directory in one a copy of the
# unit test config.
check_tidy_configs()
{
	local base=$1 d u rc=0
	for d in "${fork_dirs[@]}"; do
		if ! cmp -s "$tidy_config" "$base/$d/.clang-tidy"; then
			echo "$d/.clang-tidy: not a copy of tools/dev/style/clang-tidy;" \
				"run cp tools/dev/style/clang-tidy $d/.clang-tidy"
			rc=1
		fi
		while IFS= read -r -d '' u; do
			u=${u#"$root"/}
			cmp -s "$unittest_config" "$base/$u/.clang-tidy" && continue
			echo "$u/.clang-tidy: not a copy of tools/dev/style/clang-tidy-unittest;" \
				"run cp tools/dev/style/clang-tidy-unittest $u/.clang-tidy"
			rc=1
		done < <(find "$root/$d" -type d -name unittest -print0 2>/dev/null)
		# Any other config would switch checks off for the files below it.
		while IFS= read -r -d '' u; do
			u=${u#"$base"/}
			[[ $u == "$d/.clang-tidy" || $u == */unittest/.clang-tidy ]] && continue
			echo "$u: a .clang-tidy that is neither the fork directory's nor a unittest/" \
				"copy; remove it (fork code gets the fork checks, conventions.md)"
			rc=1
		done < <(find "$base/$d" -name .clang-tidy -print0 2>/dev/null)
	done
	return "$rc"
}

# --staged: the temporary tree holding the staged files and every
# .clang-tidy in the index, whose paths are reported as the repository's,
# and clang-tidy's options for it.
tree=
# The config's CustomChecks (ADR 5) run only with this flag.
tidy_opts=(--experimental-custom-checks)
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
	local f n line name rc=0
	for f in "$@"; do
		n=0
		name=$(rel "$f")
		while IFS= read -r line; do
			n=$((n + 1))
			[[ $line == *NOLINT* ]] || continue
			[[ $line =~ NOLINT(NEXTLINE|BEGIN)?\([^\)]+\):\ *[^\ ] ||
				$line =~ NOLINTEND\([^\)]+\) ]] && continue
			echo "$name:$n: NOLINT without a check and a reason;" \
				"write NOLINT(check): reason"
			rc=1
		done < "$f"
	done
	return "$rc"
}

# Fail on each #include or #include_next in files "$@" whose path climbs
# through `..`, with quotes or angle brackets, also split by a line
# continuation or with a comment after the #: clang-tidy reports no finding
# in a header named that way (header_filter), so fork code includes fork
# headers by their path from the top.  A path built by a macro passes
# (ADR 5).  Also fail on an upstream_begin.h not closed by upstream_end.h
# before the next one or the end of the file, and on an upstream_end.h
# without one: the strict warnings would stay off for the rest of the
# translation unit.
check_includes()
{
	python3 - "$root" "${tree:-$root}" "$@" <<'PY'
import re, sys
root, tree, files = sys.argv[1], sys.argv[2], sys.argv[3:]
include = re.compile(r'^\s*#\s*include(?:_next)?\s*([<"])([^>"]*)[>"]')
rc = 0
def name(f):
    for p in (tree + "/", root + "/"):
        if f.startswith(p):
            return f[len(p):]
    return f
for f in files:
    n = name(f)
    if n in ("tools/dev/upstream_begin.h", "tools/dev/upstream_end.h"):
        continue
    text = open(f, errors="replace").read()
    # Comments become a space, line continuations join their lines; each
    # logical line keeps the number of its first physical line.
    text = re.sub(r"/\*.*?\*/", lambda m: " " + "\n" * m.group(0).count("\n"), text, flags=re.S)
    lines, start, cur = [], 1, ""
    for i, line in enumerate(text.split("\n"), 1):
        if not cur:
            start = i
        if line.endswith("\\"):
            cur += line[:-1]
            continue
        lines.append((start, cur + line))
        cur = ""
    open_at = None
    for lno, line in lines:
        m = include.match(line)
        if not m:
            continue
        path = m.group(2)
        if re.search(r"(^|/)\.\./", path):
            print(f"{n}:{lno}: #include through ..; include fork headers by their path"
                  f' from the top ("common/debug/x.h")')
            rc = 1
        base = path.rsplit("/", 1)[-1]
        if base == "upstream_begin.h":
            if open_at is not None:
                print(f"{n}:{lno}: upstream_begin.h inside the region opened at line {open_at};"
                      " close it with upstream_end.h first")
                rc = 1
            open_at = lno
        elif base == "upstream_end.h":
            if open_at is None:
                print(f"{n}:{lno}: upstream_end.h without upstream_begin.h")
                rc = 1
            open_at = None
    if open_at is not None:
        print(f"{n}:{open_at}: upstream_begin.h not closed by upstream_end.h;"
              " the strict warnings would stay off for the rest of the file")
        rc = 1
sys.exit(rc)
PY
}


# Succeed if the compilation database has an entry for file $1.
in_database()
{
	local m
	m=$(not_in_database "$1" 2>/dev/null) && [[ -z $m ]]
}

# Print, one per line, the files among "$@" the compilation database has
# no entry for (a staged copy by its real path).
not_in_database()
{
	local f paths=()
	for f in "$@"; do
		if [[ -n $tree ]]; then paths+=("$root/$(rel "$f")"); else paths+=("$f"); fi
	done
	python3 - "$database/compile_commands.json" "$@" "${paths[@]}" <<'PY'
import json, os, sys
entries = json.load(open(sys.argv[1]))
known = {os.path.realpath(os.path.join(e["directory"], e["file"])) for e in entries}
n = (len(sys.argv) - 2) // 2
for f, p in zip(sys.argv[2:2 + n], sys.argv[2 + n:]):
    if os.path.realpath(p) not in known:
        print(f)
PY
}

# Print the regular expression for clang-tidy's --header-filter: headers
# in a fork directory, named relative to the repository (the database's
# directory), absolutely, or in the staged tree.  Upstream and third-party
# headers are not fork code, and their findings would be reported against
# the fork file including them.  A path climbing through `..` may leave the
# fork directory it starts in (src/../common/x.h), so --exclude-header-filter
# drops it: fork code includes fork headers by their path from the root.
header_filter()
{
	local esc='s/[][\\.*^$+?(){}|]/\\&/g' pre dirs
	# A doubled slash (TMPDIR's trailing one) names the same directory.
	pre=$(printf '%s/\n' "$root" ${tree:+"$tree"} | sed "$esc" | sed 's|/|/+|g' | paste -sd '|' -)
	dirs=$(printf '%s\n' "${fork_dirs[@]}" | sed "$esc" | paste -sd '|' -)
	printf '^(%s|\\./)?(%s)/\n' "$pre" "$dirs"
}

# clang-tidy the C++ files among "$@" with the compilation database; the
# clang-tidy rules are C++ rules, so C and Objective-C files are left out,
# and Objective-C++ ones are tidied only when the database has them.  A header has
# no entry of its own, and the one clang-tidy would borrow may be GCC's
# (the container), with flags clang rejects; headers are tidied with the
# fork's own flags instead (fork_warnings.py, the pinned dependencies), and
# see SDL as an sdl3=1 build does: <SDL.h>, <SDL_image.h> and <SDL_mixer.h>
# in the SDL3, SDL3_image and SDL3_mixer include directories,
# SDL_ENABLE_OLD_NAMES defined (ADR 3).
# Source files keep their database entry, less the flags clang rejects.
tidy()
{
	local f c rc=0 cxx=() headers=() configs=() filter db flag removed=() missing=()
	filter=(--header-filter="$(header_filter)" --exclude-header-filter='(^|/)\.\./')
	for f in "$@"; do
		case $f in
		*.c|*.m) ;;
		*.h|*.hh|*.hpp|*.hxx) headers+=("$f") ;;
		*.mm) in_database "$f" && cxx+=("$f") ;;
		*) cxx+=("$f") ;;
		esac
	done
	mapfile -t configs < <(for f in "${headers[@]}" "${cxx[@]}"; do config_of "$f"; done | sort -u)
	# The SDL3-only sources (tools/dev/sdl3-only) are not in build/dev's
	# database: they are tidied with build/dev-sdl3's after the others.
	local sdl3=() rest=()
	for f in "${cxx[@]+"${cxx[@]}"}"; do
		if sdl3_only "$f"; then sdl3+=("$f"); else rest+=("$f"); fi
	done
	cxx=("${rest[@]+"${rest[@]}"}")
	if [[ ${#cxx[@]} -gt 0 ]]; then
		if [[ ! -f $database/compile_commands.json ]]; then
			echo "format.sh: no $database/compile_commands.json; run tools/dev/build.sh first" >&2
			return 1
		fi
		db=$database
		# clang-tidy would borrow a neighbour's command for a file the
		# database lacks and pass the config's ExtraArgs to the driver as
		# file names, dropping the findings they turn on: such a file fails
		# untidied.  The staged tree gets a database of its own.
		[[ -n $tree ]] || mapfile -t missing < <(not_in_database "${cxx[@]}")
		if [[ ${#missing[@]} -gt 0 ]]; then
			for f in "${missing[@]}"; do
				echo "$(rel "$f"): no entry in build/dev/compile_commands.json" >&2
			done
			echo "format.sh: run tools/dev/build.sh dev to write the entries; a source built" \
				"only with sdl3=1 belongs in tools/dev/sdl3-only" >&2
			rc=1
			mapfile -t cxx < <(printf '%s\n' "${cxx[@]}" | grep -vxF -f <(printf '%s\n' "${missing[@]}"))
		fi
		if [[ -n $tree ]]; then
			db=$tree/.database
			mkdir -p "$db"
			staged_database "$database/compile_commands.json" "${cxx[@]}" \
				> "$db/compile_commands.json" || return 1
		fi
		while IFS= read -r flag; do
			removed+=(--removed-arg="$flag")
		done < <(unknown_flags "$db/compile_commands.json")
	fi
	# One run per config (see the header).
	for c in "${configs[@]+"${configs[@]}"}"; do
		tidy_headers "$c" "${headers[@]+"${headers[@]}"}" || rc=1
		tidy_sources "$c" "${cxx[@]+"${cxx[@]}"}" || rc=1
	done
	[[ ${#sdl3[@]} -gt 0 ]] || return "$rc"
	local sdl3_database=$root/build/dev-sdl3
	if [[ ! -f $sdl3_database/compile_commands.json ]]; then
		for f in "${sdl3[@]}"; do
			echo "SKIP tidy $(rel "$f"): SDL3-only, and no build/dev-sdl3/compile_commands.json" \
				"(build.sh dev sdl3=1 build/dev-sdl3/compile_commands.json writes it)"
		done
		return "$rc"
	fi
	db=$sdl3_database
	mapfile -t missing < <(database=$sdl3_database; [[ -n $tree ]] || not_in_database "${sdl3[@]}")
	for f in "${missing[@]+"${missing[@]}"}"; do
		echo "$(rel "$f"): no entry in build/dev-sdl3/compile_commands.json" >&2
		rc=1
	done
	if [[ -n $tree ]]; then
		db=$tree/.database-sdl3
		mkdir -p "$db"
		staged_database "$sdl3_database/compile_commands.json" "${sdl3[@]}" \
			> "$db/compile_commands.json" || return 1
	fi
	removed=()
	while IFS= read -r flag; do
		removed+=(--removed-arg="$flag")
	done < <(unknown_flags "$db/compile_commands.json")
	for c in "${configs[@]+"${configs[@]}"}"; do
		tidy_sources "$c" "${sdl3[@]}" || rc=1
	done
	return "$rc"
}

# True when file $1 is listed in tools/dev/sdl3-only: a C++ source built only
# with sdl3=1, so absent from build/dev's database.
sdl3_only()
{
	local r
	r=$(rel "$1")
	[[ -f $dev_dir/sdl3-only ]] && grep -qxF -e "$r" "$dev_dir/sdl3-only"
}

# Print the .clang-tidy clang-tidy reads for file $1: the nearest one in its
# directory or above.
config_of()
{
	local d
	d=$(dirname "$1")
	while [[ $d == /?* ]]; do
		[[ -f $d/.clang-tidy ]] && { printf '%s\n' "$d/.clang-tidy"; return; }
		d=$(dirname "$d")
	done
	printf 'none\n'
}

# Print the files among "${@:2}" whose config is $1.
with_config()
{
	local f
	for f in "${@:2}"; do
		[[ $(config_of "$f") == "$1" ]] && printf '%s\n' "$f"
	done
}

# clang-tidy the headers among "${@:2}" whose config is $1.
tidy_headers()
{
	local f sel=() flags=() header_flags=()
	mapfile -t sel < <(with_config "$@")
	[[ ${#sel[@]} -eq 0 ]] && return 0
	read -ra flags <<<"$(python3 "$dev_dir/fork_warnings.py")"
	[[ $dev_os == darwin ]] && flags+=(-isysroot "$(xcrun --show-sdk-path)")
	# The top, for fork headers included by their path from it
	# (conventions.md), and a staged header's own directory, for the
	# includes beside it.
	flags+=(-I "$root")
	for f in "${sel[@]}"; do
		flags+=(-I "$root/$(dirname "$(rel "$f")")")
	done
	header_flags=("${flags[@]}")
	tidy_parallel tidy_header "${sel[@]}"
}

# clang-tidy header $1 with the flags tidy_headers collected.
tidy_header()
{
	"$clang_tidy" "${tidy_opts[@]+"${tidy_opts[@]}"}" "${filter[@]}" --quiet "$1" -- \
		-x c++-header -std=c++23 "${header_flags[@]}" \
		-isystem "$dev_deps_prefix/include" -isystem "$dev_deps_prefix/include/SDL3" \
		-isystem "$dev_deps_prefix/include/SDL3_image" -isystem "$dev_deps_prefix/include/SDL3_mixer" \
		-DSDL_ENABLE_OLD_NAMES 2>&1 | strip_tree
	[[ ${PIPESTATUS[0]} -eq 0 ]]
}

# clang-tidy the source files among "${@:2}" whose config is $1, with the
# database $db less the flags clang rejects ($removed).
tidy_sources()
{
	local sel=()
	mapfile -t sel < <(with_config "$@")
	[[ ${#sel[@]} -eq 0 ]] && return 0
	tidy_parallel tidy_source "${sel[@]}"
}

# clang-tidy source $1 with the database $db less the flags clang rejects.
tidy_source()
{
	"$clang_tidy" "${tidy_opts[@]+"${tidy_opts[@]}"}" "${filter[@]}" \
		"${removed[@]+"${removed[@]}"}" -p "$db" --quiet "$1" 2>&1 | strip_tree
	[[ ${PIPESTATUS[0]} -eq 0 ]]
}

# Run function $1 on each file among "${@:2}", dev_jobs (env.sh) at a time,
# the largest files first, which keeps the last job short; print each one's
# output whole in the order the files were given, so the output does not
# depend on timing.  Fails when any run fails, or leaves no status behind.
tidy_parallel()
{
	local fn=$1 dir i rc=0 order=()
	local files=("${@:2}")
	dir=$(mktemp -d "${TMPDIR:-/tmp}/format-tidy.XXXXXX") || return 1
	mapfile -t order < <(for i in "${!files[@]}"; do
		printf '%s %s\n' "$(wc -c < "${files[i]}" 2>/dev/null | tr -d ' ' )" "$i"
	done | sort -rn | cut -d' ' -f2)
	for i in "${order[@]}"; do
		while [[ $(jobs -rp | wc -l) -ge $dev_jobs ]]; do
			wait -n
		done
		("$fn" "${files[i]}" > "$dir/$i" 2>&1; echo "$?" > "$dir/$i.rc") &
	done
	wait
	for i in "${!files[@]}"; do
		cat "$dir/$i"
		[[ $(cat "$dir/$i.rc" 2>/dev/null) == 0 ]] || rc=1
	done
	rm -rf "$dir"
	return "$rc"
}

# Print, one per line, the warning and code generation flags in compilation
# database $1 that the pinned clang-tidy rejects: in the container the
# database is GCC's (-fstrict-flex-arrays, -Wlogical-op, ...), and an
# unknown flag is an error there.  One clang-tidy run on an empty file with
# every such flag reports them all.
unknown_flags()
{
	local probe flags=()
	mapfile -t flags < <(python3 - "$1" <<'PY'
import json, shlex, sys
seen = set()
for e in json.load(open(sys.argv[1])):
    for a in e.get("arguments") or shlex.split(e["command"]):
        if a[:2] in ("-W", "-f", "-m") and "," not in a and a not in seen:
            seen.add(a)
            print(a)
PY
	)
	[[ ${#flags[@]} -eq 0 ]] && return 0
	probe=$(mktemp "${TMPDIR:-/tmp}/format-probe.XXXXXX") || return 1
	"$clang_tidy" --config='{Checks: "-*,modernize-use-nullptr,clang-diagnostic-*"}' "$probe" \
		-- -x c++ "${flags[@]}" 2>&1 |
		sed -nE "s/.*unknown (argument:|warning option) '([^']+)'.*/\2/p" | sort -u
	rm -f "$probe"
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
	if [[ $mode == tidy ]]; then
		check_tidy_configs "$root" || rc=1
		[[ ${#files[@]} -eq 0 ]] && exit "$rc"
	else
		tree=$(mktemp -d "${TMPDIR:-/tmp}/format-staged.XXXXXX") || exit 1
		trap 'rm -rf "$tree"' EXIT
		staged=("${files[@]#"$root"/}")
		mapfile -d '' -t configs < <(git -C "$root" ls-files -z -- '*.clang-tidy')
		paths=("${staged[@]}" "${configs[@]+"${configs[@]}"}")
		if [[ ${#paths[@]} -gt 0 ]]; then
			git -C "$root" checkout-index --prefix="$tree/" -- "${paths[@]}" || exit 1
		fi
		# A config never committed nor staged (a new fork directory's) is
		# read from the working tree, as clangd reads it.
		while IFS= read -r -d '' f; do
			f=${f#"$root"/}
			[[ -e $tree/$f ]] && continue
			git -C "$root" cat-file -e "HEAD:$f" 2>/dev/null && continue
			mkdir -p "$tree/$(dirname "$f")" && cp "$root/$f" "$tree/$f"
		done < <(cd "$root" && find "${fork_dirs[@]/#/$root/}" -name .clang-tidy -print0 2>/dev/null)
		check_tidy_configs "$tree" || rc=1
		[[ ${#files[@]} -eq 0 ]] && exit "$rc"
		files=("${staged[@]/#/$tree/}")
		for f in "${staged[@]}"; do
			"$clang_format" "$style" --assume-filename="$f" --dry-run --Werror < "$tree/$f" ||
				{ echo "$f: not formatted; run tools/dev/format.sh"; rc=1; }
		done
	fi
	check_nolint "${files[@]}" || rc=1
	check_includes "${files[@]}" || rc=1
	tidy "${files[@]}" || rc=1
	exit "$rc" ;;
esac
