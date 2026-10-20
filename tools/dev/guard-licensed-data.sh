#!/usr/bin/env bash
# Reject licensed game data and anything derived from it (rubric R15).
#
#   guard-licensed-data.sh FILE...    check files in the working tree
#   guard-licensed-data.sh --staged   check files added or changed in the index
#
# A file is rejected when its extension is a Descent data, movie, level or
# demo format, when it starts with the magic bytes of a game archive, or
# when it lies in a captures/, goldens/ or traces/ directory or is a trace
# (.rtr), unless its path contains a synthetic/ directory.  Rejected paths
# are printed with the reason; the exit status is 1 if any was rejected.
set -u

# Extensions of Descent 1/2 game data, movies, levels and demos.
data_ext_re='\.(hog|pig|ham|s11|s22|mvl|mve|rdl|rl2|dem|txb)$'
# Directories and extensions holding captures and data derived from them.
derived_re='(^|/)(captures|goldens|traces)/|\.rtr$'
synthetic_re='(^|/)synthetic/'

# Print the reason PATH is rejected, given its first bytes as hex in HEAD.
reason()
{
	local path=$1 head=$2 lower
	lower=$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')
	if [[ $lower =~ $data_ext_re ]]; then
		echo "game data extension ${BASH_REMATCH[1]}"
	elif [[ $head == 444846* ]]; then
		echo 'HOG archive magic (DHF)'
	elif [[ $head == 50504947* ]]; then
		echo 'PIG archive magic (PPIG)'
	elif [[ $head == 48414d21* ]]; then
		echo 'HAM archive magic (HAM!)'
	elif [[ $head == 444d564c* ]]; then
		echo 'MVL archive magic (DMVL)'
	elif [[ $path =~ $derived_re && ! $path =~ $synthetic_re ]]; then
		echo 'capture, trace or golden outside synthetic/'
	fi
}

rejected=0

# Print stdin's first four bytes as lowercase hex without spaces.
magic()
{
	head -c 4 | od -An -tx1 | tr -d ' \n'
}

check()
{
	local path=$1 head=$2 why
	why=$(reason "$path" "$head")
	if [[ -n $why ]]; then
		printf '%s: %s\n' "$path" "$why" >&2
		rejected=1
	fi
}

if [[ ${1-} == --staged ]]; then
	while IFS= read -r -d '' path; do
		check "$path" "$(git cat-file blob ":$path" | magic)"
	done < <(git diff --cached --name-only --diff-filter=ACMR -z)
else
	for path in "$@"; do
		check "$path" "$(magic 2>/dev/null < "$path")"
	done
fi

if [[ $rejected -ne 0 ]]; then
	echo 'Licensed game data and data derived from it stay out of git (conventions.md).' >&2
fi
exit "$rejected"
