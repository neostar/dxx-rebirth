#!/usr/bin/env bash
# Run a binary the way every dev and probe run should: windowless.
#
#   run.sh BINARY [ARG...]
#
# Sources env.sh (sanitizer settings without abort()), checks that every
# library the binary links resolves (macOS: otool -L; Linux: ldd; else the
# check is skipped with a note), then runs it under no-window.sh.  An
# unresolved library fails with exit status 1 before launch, instead of a
# dyld crash and its dialog.
set -u
dev_dir=$(cd "$(dirname "$0")" && pwd)
source "$dev_dir/env.sh"
[[ $# -gt 0 ]] || { echo "usage: $0 BINARY [ARG...]" >&2; exit 2; }
bin=$1
[[ $bin == */* ]] || bin=$(command -v "$bin") || { echo "run.sh: $1: not found" >&2; exit 1; }

# Print each library of Mach-O binary $1 that does not resolve.  Libraries
# under /usr/lib and /System live in the dyld shared cache, not on disk.
unresolved_macho()
{
	local exe_dir lib lib2 rp found rpaths=()
	exe_dir=$(cd "$(dirname "$1")" && pwd)
	while read -r rp; do
		rpaths+=("${rp/@executable_path/$exe_dir}")
	done < <(otool -l "$1" | awk '$1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }')
	while read -r lib; do
		case $lib in
		/usr/lib/*|/System/*) continue ;;
		@rpath/*)
			found=
			for rp in "${rpaths[@]+"${rpaths[@]}"}"; do
				rp=${rp/@loader_path/$exe_dir}
				[[ -e $rp/${lib#@rpath/} ]] && found=1
			done
			[[ -n $found ]] || echo "$lib" ;;
		@executable_path/*|@loader_path/*)
			lib2=${lib/@executable_path/$exe_dir}
			[[ -e ${lib2/@loader_path/$exe_dir} ]] || echo "$lib" ;;
		*) [[ -e $lib ]] || echo "$lib" ;;
		esac
	done < <(otool -L "$1" | awk 'NR > 1 { print $1 }')
}

if [[ $(uname -s) == Darwin ]] && command -v otool >/dev/null; then
	missing=$(unresolved_macho "$bin")
elif command -v ldd >/dev/null; then
	missing=$(ldd "$bin" 2>/dev/null | awk '/not found/ { print $1 }')
else
	echo "run.sh: SKIP library check: neither otool nor ldd found" >&2
	missing=
fi
if [[ -n $missing ]]; then
	echo "run.sh: $bin: unresolved libraries:" >&2
	printf '  %s\n' $missing >&2
	exit 1
fi
exec "$dev_dir/no-window.sh" "$bin" "${@:2}"
