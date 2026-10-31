# SCons settings for every tools/dev/build.sh build, read after the
# profile's site files: the pinned dependency prefix's lib directory goes
# ahead of every other -L, so that -lphysfs finds the pinned, patched
# PhysicsFS (ticket #87) and not Homebrew's, whose lib directory libpng and
# SDL's .pc files also add.  build.sh exports DXX_DEPS_PREFIX; the flag goes
# into SCons' default environment, as site-sdl3.py's does, once however often
# SCons reads this file.  SCons splits the flags at spaces, so the path may
# not hold one.
import os
import sys

import SCons.Defaults

_prefix = os.environ.get('DXX_DEPS_PREFIX', '')
if not _prefix:
	sys.exit('site-deps.py: DXX_DEPS_PREFIX is not set; build.sh sets it')
_env = SCons.Defaults.ConstructionEnvironment
_libdir = '-L' + os.path.join(_prefix, 'lib') + ' '
if not _env['_LIBDIRFLAGS'].startswith(_libdir):
	_env['_LIBDIRFLAGS'] = _libdir + _env['_LIBDIRFLAGS']
