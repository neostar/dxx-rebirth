# SCons settings for the fork's fast development build (tools/dev/build.sh).
# Read by `scons site=tools/dev/site-dev.py`; affects nobody who does not pass it.
# D2X only (d1x=0 d2x=1) cannot be set here: SConstruct reads d1x and d2x
# from the command line alone, so build.sh passes them.
builddir = 'build/dev'
lto = '0'
ccache = 'ccache'
# Keep the version string constant so an edit does not rebuild vers_id.cpp
# and relink.
git_describe_version = False
git_status = False
compilation_database = 'build/dev/compile_commands.json'
register_runtime_test_link_targets = True
# Homebrew libraries instead of frameworks, as upstream's macOS CI does.
macos_add_frameworks = False
macos_bundle_libs = False
# Apple clang folds __builtin_constant_p after inlining, so the unit tests'
# deliberate out-of-range calls keep a DXX_ALWAYS_ERROR_FUNCTION reference
# and fail to link.  Turn the compile-time range checks off for this build.
sconf_optimize_builtin_constant_p = 'force-failure'
# Strict warnings for fork code (ADR 5); SCons puts this file's directory
# on sys.path while it reads it.
import fork_warnings
fork_warnings.install()
