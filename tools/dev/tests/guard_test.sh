#!/usr/bin/env bash
# Command-line behaviour of tools/dev/guard-licensed-data.sh: it rejects
# licensed game data, demos, captures and goldens derived from them.
source "$(dirname "$0")/lib.sh"
GUARD="$DEV_DIR/guard-licensed-data.sh"

# Test that ordinary source files pass and the guard prints nothing.
test_source_files_pass()
{
	local d out
	d=$(make_scratch)
	printf 'int main() {}\n' > "$d/main.cpp"
	printf '# notes\n' > "$d/README.md"
	out=$("$GUARD" "$d/main.cpp" "$d/README.md" 2>&1)
	assert_eq "$?" 0
	assert_eq "$out" ""
}

# Test that game archives and demos are rejected by extension, each named.
test_game_data_extensions_rejected()
{
	local d out rc f
	d=$(make_scratch)
	for f in descent2.hog groupa.pig descent2.ham descent2.s11 hires.mvl intro-h.mve level01.rl2 game.dem DESCENT.HOG; do
		printf 'x' > "$d/$f"
	done
	out=$("$GUARD" "$d"/* 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	for f in descent2.hog groupa.pig descent2.ham descent2.s11 hires.mvl intro-h.mve level01.rl2 game.dem DESCENT.HOG; do
		assert_contains "$out" "$f"
	done
}

# Test that renamed game archives are caught by their magic bytes.
test_game_data_magic_rejected()
{
	local d out rc
	d=$(make_scratch)
	printf 'DHF\0\0\0\0' > "$d/a.bin"
	printf 'PPIG\2\0\0\0' > "$d/b.dat"
	printf 'HAM!\3\0\0\0' > "$d/c"
	printf 'DMVL\1\0\0\0' > "$d/d.txt"
	out=$("$GUARD" "$d/a.bin" "$d/b.dat" "$d/c" "$d/d.txt" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "a.bin"
	assert_contains "$out" "b.dat"
	assert_contains "$out" "/c:"
	assert_contains "$out" "d.txt"
}

# Test that magic bytes are read without xxd (part of vim, not POSIX).
test_magic_detection_needs_no_xxd()
{
	local d bin out rc
	d=$(make_scratch); bin=$(make_scratch)
	printf '#!/bin/sh\nexit 127\n' > "$bin/xxd"; chmod +x "$bin/xxd"
	printf 'DHF\0\0\0\0' > "$d/a.bin"
	out=$(PATH="$bin:$PATH" "$GUARD" "$d/a.bin" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1 "$out"
	assert_contains "$out" "HOG archive magic"
}

# Test that captures, traces and goldens are rejected unless synthetic.
test_captures_and_goldens_rejected_unless_synthetic()
{
	local d out rc
	d=$(make_scratch)
	mkdir -p "$d/captures" "$d/goldens" "$d/synthetic/goldens"
	printf 'x' > "$d/captures/frame1.png"
	printf 'x' > "$d/level1.rtr"
	printf 'x' > "$d/goldens/corridor.png"
	printf 'x' > "$d/synthetic/quad.rtr"
	printf 'x' > "$d/synthetic/goldens/quad.png"
	out=$("$GUARD" "$d/captures/frame1.png" "$d/level1.rtr" "$d/goldens/corridor.png" 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "captures/frame1.png"
	assert_contains "$out" "level1.rtr"
	assert_contains "$out" "goldens/corridor.png"
	out=$("$GUARD" "$d/synthetic/quad.rtr" "$d/synthetic/goldens/quad.png" 2>&1)
	assert_eq "$?" 0
}

# Test that --staged checks the index of the current repository, so a
# staged archive fails even after the working copy is renamed away.
test_staged_mode_checks_index()
{
	local r out rc
	r=$(make_repo) || die "make_repo failed"
	printf 'int x;\n' > "$r/ok.cpp"
	tgit -C "$r" add ok.cpp
	out=$(cd "$r" && "$GUARD" --staged 2>&1)
	assert_eq "$?" 0
	printf 'DHF\0\0\0\0' > "$r/data.bin"
	tgit -C "$r" add data.bin
	rm "$r/data.bin"
	out=$(cd "$r" && "$GUARD" --staged 2>&1) && rc=0 || rc=$?
	assert_eq "$rc" 1
	assert_contains "$out" "data.bin"
}

# Test that staged deletions of game data are allowed (they remove it).
test_staged_deletion_passes()
{
	local r out
	r=$(make_repo) || die "make_repo failed"
	printf 'x' > "$r/old.hog"
	tgit -C "$r" add old.hog
	tgit -C "$r" commit -q -m add --no-verify
	tgit -C "$r" rm -q old.hog
	out=$(cd "$r" && "$GUARD" --staged 2>&1)
	assert_eq "$?" 0
}

# Test that git ignores licensed game data and captures, traces and goldens
# derived from it, but not synthetic goldens (read-only check of the
# repository's .gitignore).
test_gitignore_covers_licensed_data()
{
	local p
	for p in descent2.hog d2/groupa.pig descent2.ham hires.mvl game.dem level01.rdl level01.rl2 \
		traces/local/run.rtr captures/frame1.png goldens/corridor.png d2x-rebirth/goldens/x.png; do
		(real_git check-ignore -q --no-index "$p") || fail "$p is not ignored"
	done
	for p in tools/dev/tests/synthetic/goldens/quad.png traces/synthetic/quad.rtr; do
		(real_git check-ignore -q --no-index "$p") && fail "$p is ignored"
	done
	return 0
}

run_tests
