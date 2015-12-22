#!/usr/bin/python
# Local Variables:
# mode: python
# End:

import os
import subprocess
import re
import glob


class llctx:
    def __init__(self, dbg_level = 10):
        self.cmdline = read_kernel_cmdline()
        self.dbg_level = dbg_level

    def dbg_print(lev, str, newl = '\n'):
        if lev < dbg_level:
            os.sys.stderr.write(str)
            if newl:
                os.sys.stderr.write(newl)

    def set_task_affinity(pid, cpus):
        cpulist = ','.join(map(str, cpus))
        try:
            subprocess.check_output(['taskset', '-c', cpulist, '-p', str(pid)])
        except CalledProcessError as e:
            dbg_print(0, 'Failed to set task affinity:' + e.output)
            raise


def read_kernel_cmdline():
    cc = open('/proc/cmdline', 'r')
    cmdline = cc.read()
    cc.close()
    return cmdline


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('-i', '--incfg', type=str, action='append', default=[],
                        help='Adds one configuration file')
    parser.add_argument('-r', '--root', type=str, default='.',
                        help='Sets the source root directory')
    parser.add_argument('-c', '--cmd', type=str, default='verify',
                        help='Run the verify command')
    parser.add_argument('-C', '--configs', type=str, action='append', default=[],
                        help='Sets the config name(s) to check')
    parser.add_argument('-o', '--outcfg', type=str,
                        help='Sets the output configuration file ("-" means STDOUT)')
    parser.add_argument('-L', '--log_level', type=int, default=1,
                        help='Sets the logging level')
    args = parser.parse_args()


if __name__ == '__main__':
    main()
