# SCons settings for sdl3=1 builds (tools/dev/build.sh), read after the
# profile's site files, so every profile keeps its own flags (site-asan.py's
# -fsanitize among them); this file sets none of them.  build.sh exports:
#
# DXX_SDL3_PREFIX: the pinned prefix.  Its lib directory goes ahead of
#   every other -L: physfs and libpng add Homebrew's lib directory, which
#   holds Homebrew's SDL3 and SDL3_mixer, and the pinned SDL3_image would
#   then load the pinned libSDL3 beside Homebrew's, two SDL3s in one
#   process.
# DXX_SDL3_SYSTEM_INCLUDE (macOS): Homebrew's include directory, made a
#   system one, as /usr/include is on Linux: SDL3's headers, unlike SDL2's,
#   pass SConstruct's -Wold-style-cast probe, and Homebrew's Boost then
#   fails its configure check under -Werror.
#
# Both go into SCons' default environment, as fork_warnings.py's flags do,
# once however often SCons reads this file.  SCons splits the flags at
# spaces, so neither path may hold one.
import os
import sys

import SCons.Defaults

_prefix = os.environ.get('DXX_SDL3_PREFIX', '')
if not _prefix:
	sys.exit('site-sdl3.py: DXX_SDL3_PREFIX is not set; build.sh sets it for sdl3=1')
_env = SCons.Defaults.ConstructionEnvironment
_libdir = '-L' + os.path.join(_prefix, 'lib') + ' '
if not _env['_LIBDIRFLAGS'].startswith(_libdir):
	_env['_LIBDIRFLAGS'] = _libdir + _env['_LIBDIRFLAGS']
_include = os.environ.get('DXX_SDL3_SYSTEM_INCLUDE', '')
if _include and not _env['_CPPINCFLAGS'].startswith('-isystem ' + _include + ' '):
	_env['_CPPINCFLAGS'] = '-isystem ' + _include + ' ' + _env['_CPPINCFLAGS']
