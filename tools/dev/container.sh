#!/usr/bin/env bash
# Build and use the Linux reference container (tools/dev/container/Dockerfile),
# where tools/dev/ci.sh runs its Linux leg (ADR 4).
#
#   container.sh build          build the image (fetches the sources first),
#                               then remove older dxx-rebirth-ref images
#   container.sh tag            print the image tag for the current inputs
#   container.sh run [--clean] CMD...     run CMD offline in /work
#   container.sh check [--clean] [--fast] tools/dev/check.sh there
#   container.sh prune          remove dangling images and unused build cache
#   container.sh save           export the current image to third_party/images,
#                               replacing the one exported before
#   container.sh load           import that export, without snapshot.ubuntu.com
#
# The image is tagged by a hash of everything it is built from (Dockerfile,
# manifest, lockfile, deps.sh, env.sh), so a stale image is never used; it
# stays local and is never pushed to a registry.  run and check start a
# fresh container with --network none, a fixed locale (C.UTF-8) and
# timezone (UTC), and never mount the worktree:
#   default: /work is a fresh repository holding a copy of the working
#     tree (tracked and untracked files, not ignored ones), committed;
#   --clean: /work is a fresh `git clone` of the commit under test (HEAD)
#     from the clone's git directory, mounted read-only; uncommitted
#     changes are excluded.
# Neither keeps a build cache.  The Docker daemon must be running (on the
# Mac: `orb start`).
set -eu
dev_dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dev_dir/../.." && pwd)
source "$dev_dir/env.sh"
manifest=$dev_dir/toolchain.pins

usage()
{
	echo "usage: $0 build|tag|run [--clean] CMD...|check [--clean] [--fast]|prune|save|load" >&2
	exit 2
}

# Print the reference pinned by `image <name>` in the manifest.
image_ref()
{
	awk -v n="$1" '$1 == "image" && $2 == n {print $3}' "$manifest"
}

# The one exported image: third_party/images, gitignored.
images_dir=$root/third_party/images

image_tag()
{
	echo "dxx-rebirth-ref:$(cat "$dev_dir/container/Dockerfile" "$manifest" \
		"$dev_dir/requirements.lock" "$dev_dir/deps.sh" "$dev_dir/env.sh" |
		shasum -a 256 | cut -c1-12)"
}

# Run the remaining arguments offline in the image, in /work (see above).
run_offline()
{
	local tag clean=0
	[[ ${1-} == --clean ]] && { clean=1; shift; }
	tag=$(image_tag)
	docker image inspect "$tag" >/dev/null 2>&1 ||
		{ echo "container.sh: no image $tag; run tools/dev/container.sh build" >&2; exit 1; }
	if [[ $clean -eq 1 ]]; then
		echo "container.sh: --clean checks commit $(git -C "$root" rev-parse --short HEAD); uncommitted changes are excluded"
		docker run --rm --network none --platform linux/amd64 \
			-v "$(git -C "$root" rev-parse --path-format=absolute --git-common-dir):/src.git:ro" \
			-e CI_COMMIT="$(git -C "$root" rev-parse HEAD)" \
			"$tag" bash -c '
			set -e
			git clone -q --no-checkout /src.git /work && cd /work
			git -c advice.detachedHead=false checkout -q --detach "$CI_COMMIT"
			tools/dev/setup.sh >/dev/null
			exec "$@"' bash "$@" < /dev/null
	else
		# No extended attributes: Linux tar warns on macOS ones.
		(cd "$root" && git ls-files -z -co --exclude-standard |
			COPYFILE_DISABLE=1 tar --no-xattrs --null -T - -cf -) |
			docker run --rm -i --network none --platform linux/amd64 "$tag" bash -c '
			set -e
			mkdir /work && tar -x -C /work && cd /work
			git init -q -b main && git add -A
			git -c user.name=ci -c user.email=ci@example.invalid commit -q -m worktree
			tools/dev/setup.sh >/dev/null
			exec "$@"' bash "$@"
	fi
}

case "${1-}" in
build)
	"$dev_dir/deps.sh" --platform linux-x86_64 fetch
	docker build --platform linux/amd64 \
		--build-context deps-cache="$dev_deps_cache" \
		--build-arg BASE="$(image_ref ubuntu)" --build-arg UV="$(image_ref uv)" \
		--build-arg SNAPSHOT="$(image_ref ubuntu-snapshot)" \
		-t "$(image_tag)" -f "$dev_dir/container/Dockerfile" "$dev_dir"
	# Only the image for the current inputs is ever used.
	tag=$(image_tag)
	docker image ls --format '{{.Repository}}:{{.Tag}}' dxx-rebirth-ref |
		while read -r old; do
			[[ $old == "$tag" ]] || docker rmi "$old" >/dev/null
		done
	;;
tag) image_tag ;;
prune)
	docker image prune -f
	docker builder prune -f
	;;
save)
	tag=$(image_tag)
	file=$images_dir/${tag/:/-}.tar.zst
	mkdir -p "$images_dir"
	docker save "$tag" | zstd -q -T0 -o "$file.part"
	find "$images_dir" -name 'dxx-rebirth-ref-*' ! -name "$(basename "$file").part" -delete
	mv "$file.part" "$file"
	echo "container.sh: saved $tag to $file"
	;;
load)
	tag=$(image_tag)
	file=$images_dir/${tag/:/-}.tar.zst
	[[ -f $file ]] || { echo "container.sh: no export $file; run container.sh save" >&2; exit 1; }
	zstd -dcq "$file" | docker load
	;;
run)
	shift
	opts=()
	[[ ${1-} == --clean ]] && { opts=(--clean); shift; }
	[[ $# -gt 0 ]] || usage
	run_offline "${opts[@]+"${opts[@]}"}" "$@"
	;;
check)
	shift
	opts=() fast=()
	while [[ $# -gt 0 ]]; do
		case $1 in
		--clean) opts=(--clean) ;;
		--fast) fast=(--fast) ;;
		*) usage ;;
		esac
		shift
	done
	run_offline "${opts[@]+"${opts[@]}"}" tools/dev/check.sh "${fast[@]+"${fast[@]}"}"
	;;
*) usage ;;
esac
