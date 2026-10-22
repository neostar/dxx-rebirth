#!/usr/bin/env python3
# Print a process's memory in KiB, as nightly.sh's endurance run samples it.
#
#   footprint.py PID
#
# On macOS its physical footprint (proc_pid_rusage's ri_phys_footprint,
# what Activity Monitor calls Memory): RSS leaves out pages the system
# compressed or swapped, so a leak the compressor absorbs would look like
# memory going down.  Elsewhere its RSS (/proc/PID/status VmRSS).
# Exit status 1 when the process is gone or unreadable.
import ctypes
import ctypes.util
import sys

RUSAGE_INFO_V0 = 0


class rusage_info_v0(ctypes.Structure):
	_fields_ = [('ri_uuid', ctypes.c_uint8 * 16)] + [(name, ctypes.c_uint64) for name in (
		'ri_user_time', 'ri_system_time', 'ri_pkg_idle_wkups', 'ri_interrupt_wkups',
		'ri_pageins', 'ri_wired_size', 'ri_resident_size', 'ri_phys_footprint',
		'ri_proc_start_abstime', 'ri_proc_exit_abstime')]


def kib(pid):
	if sys.platform == 'darwin':
		libc = ctypes.CDLL(ctypes.util.find_library('c'), use_errno=True)
		info = rusage_info_v0()
		if libc.proc_pid_rusage(pid, RUSAGE_INFO_V0, ctypes.byref(info)) != 0:
			return None
		return info.ri_phys_footprint // 1024
	try:
		with open(f'/proc/{pid}/status') as f:
			for line in f:
				if line.startswith('VmRSS:'):
					return int(line.split()[1])
	except OSError:
		return None
	return None


def main():
	if len(sys.argv) != 2 or not sys.argv[1].isdigit():
		print('usage: footprint.py PID', file=sys.stderr)
		return 2
	k = kib(int(sys.argv[1]))
	if k is None:
		print(f'footprint: cannot read process {sys.argv[1]}', file=sys.stderr)
		return 1
	print(k)
	return 0


if __name__ == '__main__':
	sys.exit(main())
