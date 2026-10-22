# The unit tests' runner for the `check` target: SConstruct runs each test
# program as `$DXX_TEST_RUNNER $SOURCE`, and install() sets that to run.sh,
# so a test whose libraries do not resolve fails before launch and one that
# leaves a crash report or takes focus fails too (macOS; on Linux run.sh
# checks the libraries with ldd and runs the program as before).  Unset, as
# without a site file, it expands to nothing and SCons runs the program
# directly, as upstream does.  site-dev.py calls install().  SCons splits
# the runner at spaces, so the clone's path must have none (hardening.py).
import os

RUN_SH = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'run.sh')


def install():
	import SCons.Defaults
	SCons.Defaults.ConstructionEnvironment['DXX_TEST_RUNNER'] = RUN_SH
