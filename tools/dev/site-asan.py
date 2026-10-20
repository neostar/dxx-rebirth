# SCons settings for the AddressSanitizer + UndefinedBehaviorSanitizer build
# (tools/dev/build.sh asan).  Separate build directory; debug=1 keeps asserts.
builddir = 'build/asan'
lto = '0'
ccache = 'ccache'
git_describe_version = False
git_status = False
debug = True
CXXFLAGS = '-O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined'
LINKFLAGS = '-fsanitize=address,undefined'
compilation_database = 'build/asan/compile_commands.json'
register_runtime_test_link_targets = True
# Homebrew libraries instead of frameworks, as upstream's macOS CI does.
macos_add_frameworks = False
macos_bundle_libs = False
# Apple clang folds __builtin_constant_p after inlining, so the unit tests'
# deliberate out-of-range calls keep a DXX_ALWAYS_ERROR_FUNCTION reference
# and fail to link.  Turn the compile-time range checks off for this build.
sconf_optimize_builtin_constant_p = 'force-failure'
