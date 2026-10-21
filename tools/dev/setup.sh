#!/usr/bin/env bash
# One-time setup of a clone or worktree for fork development: writes the
# key=value rules of tools/dev/repo-config (pre-commit and pre-push hooks,
# rerere) into the clone's local config, which its worktrees share.
# check.sh --fast checks them, and that the !section rules hold.  On macOS
# it also downloads Xcode's Metal toolchain when xcrun cannot run the metal
# compiler (Xcode ships without it): tools/shaders/check.sh compiles the
# MSL shaders with it.  DXX_SETUP_NO_DOWNLOAD=1 skips the download: the
# shell tests set it (tests/lib.sh), as they run setup.sh with the real PATH
# and must never change the system.
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
cd "$(git rev-parse --show-toplevel)"
while IFS= read -r rule; do
	[[ $rule == *=* && $rule != \#* ]] || continue
	git config "${rule%%=*}" "${rule#*=}"
	echo "${rule%%=*} = ${rule#*=}"
done < "$dev_dir/repo-config"

# Probe by running metal: Xcode 27 ships a metal shim in its default
# toolchain, which `xcrun -f metal` finds without the toolchain too; run,
# it fails with "missing Metal Toolchain".
if [[ $(uname -s) == Darwin && ${DXX_SETUP_NO_DOWNLOAD-} != 1 ]] &&
	! xcrun metal --version >/dev/null 2>&1; then
	echo "setup.sh: downloading Xcode's Metal toolchain (xcrun metal)"
	xcodebuild -downloadComponent MetalToolchain
fi
