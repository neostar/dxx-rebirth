# SCons overrides for the AddressSanitizer + UndefinedBehaviorSanitizer
# build (tools/dev/build.sh asan), read after site-dev.py: everything not
# set here comes from there.  Separate build directory; debug keeps asserts.
builddir = 'build/asan'
compilation_database = 'build/asan/compile_commands.json'
debug = True
CXXFLAGS = '-O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined'
LINKFLAGS = '-fsanitize=address,undefined'
