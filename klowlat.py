#!/usr/bin/python
# Local Variables:
# mode: python
# End:
#
# Kernel cmdline: iocpus=CPU_LIST nowatchdog
#

import os
import subprocess
import re
import time
import math
import glob


class llctx:
    def __init__(self, dbg_level = 10):
        self.dbg_level = dbg_level
        self.mcp_interval = 0
        self.cmdline = self.read_kernel_cmdline()
        self.num_cpus = self.get_cpu_cpunt()
        self.io_cpuset = 'io'
        self.ll_cpuset = 'll'
        self.iocpus = self.get_io_cpus(self.cmdline, self.num_cpus)
        self.llcpus = self.sub_cpus(range(0, self.num_cpus), self.iocpus)
        self.cgroup_mntslist = self.find_cgroup_mntpoint(self.parse_mounts())

    def setup(self):
        self.setup_machine_check_polls(self.llcpus, self.mcp_interval)
        self.relocate_rcu_tasks(self.iocpus)
        self.relocate_irqs(self.iocpus)
        self.cgroup_setup(self.iocpus, self.llcpus)

    def dbg_print(self, lev, str, newl = '\n'):
        if lev < self.dbg_level:
            os.sys.stderr.write(str)
            if newl:
                os.sys.stderr.write(newl)

    def get_cpu_cpunt(self):
        count = 0
        ci = open('/proc/cpuinfo', 'r')
        for line in ci:
            if re.match('^processor\s+', line):
                count += 1
        ci.close()
        return count

    def read_kernel_cmdline(self):
        cc = open('/proc/cmdline', 'r')
        cmdline = cc.read()
        cc.close()
        return cmdline

    def setup_machine_check_polls(self, cpus, intv):
        for cpu in cpus:
            path = os.path.join('/sys/devices/system/machinecheck',
                                'machinecheck' + str(cpu), 'check_interval')
            try:
                self.write_file(path, str(intv))
            except IOError as e:
                self.dbg_print(2, 'Failed to set MCP interval on CPU ' + str(cpu))

    def get_io_cpus(self, cmdln, ncpus):
        iocpus = []
        m = re.search('iocpus=([^\s]+)', cmdln)
        if m:
            for tok in m.group(1).split(','):
                mx = re.match('^(\d+)$', tok)
                if mx:
                    iocpus.append(int(mx.group(1)))
                else:
                    mx = re.match('^(\d+)-(\d+)$', tok)
                    if mx:
                        iocpus.extend(range(int(mx.group(1)),
                                            int(mx.group(2)) + 1))
        else:
            self.dbg_print(1, 'Missing iocpus config in kernel '
                           'command line: ' + cmdln)
            nio = int(math.ceil(ncpus / 10.0))
            self.dbg_print(1, 'Using about 10% of total CPUs: ' + str(nio))
            iocpus.extend(range(0, nio))
        return iocpus

    def sub_cpus(self, bset, sset):
        rset = []
        for b in bset:
            add = True
            for s in sset:
                if s == b:
                    add = False
                    break
            if add:
                rset.append(b)
        return rset

    def append_cpus(self, clist, base, last):
        if clist:
            clist += ','
        if base == last:
            clist += str(base)
        else:
            clist += str(base) + '-' + str(last)
        return clist

    def mk_cpu_list(self, cpus):
        clist = ''
        base = -1
        last = -1
        for cpu in sorted(cpus):
            if last == -1:
                base = cpu
                last = cpu
            elif (last + 1) == cpu:
                last += 1
            else:
                clist = self.append_cpus(clist, base, last)
                base = cpu
                last = cpu
        if base != -1:
            clist = self.append_cpus(clist, base, last)
        return clist

    def exec_cmd(self, xlist, what = None):
        ostr = None
        try:
            ostr = subprocess.check_output(xlist)
        except subprocess.CalledProcessError as e:
            self.dbg_print(0, 'Command: ' + ' '.join(map(str, xlist)) +
                           ' = ' + str(e.returncode))
            if what:
                self.dbg_print(0, what + ': ', newl = None)
            self.dbg_print(0, e.output)
            raise
        return ostr

    def set_task_affinity(self, pid, cpus):
        clist = ','.join(map(str, cpus))
        self.exec_cmd(['taskset', '-pc', clist, str(pid)],
                      what = 'Failed to set task affinity')

    def enum_tasks(self):
        plist = []
        for pfn in glob.glob('/proc/*'):
            m = re.match('^/proc/(\d+)', pfn)
            if m:
                plist.append(int(m.group(1)))
        return plist

    def is_kernel_task(self, pid):
        cmdln = None
        try:
            cf = open(os.path.join('/proc', str(pid), 'cmdline'))
            cmdln = cf.read()
            cf.close()
        except:
            return -1
        return 0 if len(cmdln) > 0 else 1

    def find_user_tasks(self):
        plist = []
        for pid in self.enum_tasks():
            if self.is_kernel_task(pid) == 0:
                plist.append(pid)
        return plist

    def find_tasks_by_comm(self, mrx):
        plist = []
        for pid in self.enum_tasks():
            comm = None
            try:
                cf = open(os.path.join('/proc', str(pid), 'comm'))
                comm = cf.read()
                cf.close()
            except:
                continue
            if re.match(mrx, comm):
                plist.append(pid)
        return plist

    def relocate_rcu_tasks(self, cpus):
        plist = self.find_tasks_by_comm('^rcu.*/\d+')
        for pid in plist:
            if self.is_kernel_task(pid) > 0:
                self.set_task_affinity(pid, cpus)
        return plist

    def write_file(self, path, str):
        f = open(path, 'w');
        try:
            f.write(str)
        except:
            f.close()
            raise
        f.close()

    def relocate_irqs(self, cpus):
        clist = self.mk_cpu_list(cpus)
        for pfn in glob.glob('/proc/irq/*'):
            m = re.match('^/proc/irq/(\d+)', pfn)
            if not m:
                continue
            irq = int(m.group(1))
            try:
                self.write_file(os.path.join(pfn, 'smp_affinity_list'), clist)
            except IOError as e:
                self.dbg_print(2, 'Failed to relocate IRQ ' + str(irq))

    def read_interrupts(self):
        iset = dict()
        f = open('/proc/interrupts', 'r')
        for line in f:
            m = re.match('\s*([^\s:]+):\s*([^\s].*)', line)
            if not m:
                continue
            iname = m.group(1)
            iseq = m.group(2)
            ivals = []
            iset[iname] = ivals
            while True:
                m = re.match('\s*(\d+)(.*)', iseq)
                if not m:
                    break
                ivals.append(int(m.group(1)))
                iseq = m.group(2)
        f.close()
        return iset

    def print_no_zero(self, lst, pre = ''):
        pstr = ''
        i = -1
        for n in lst:
            i += 1
            if n == 0:
                continue
            if pstr:
                pstr += ', '
            pstr += pre + str(i) + ':' + str(n)
        return pstr

    def diff_interrupts(self, bl, al):
        dstr = ''
        for a in sorted(al.keys()):
            ail = al[a]
            dil = []
            if not a in bl:
                dil = ail
            else:
                bil = bl[a]
                for i in range(0, len(ail)):
                    dil.append(ail[i] - bil[i])
            pstr = self.print_no_zero(dil, pre = 'CPU')
            if pstr:
                dstr += '[' + str(a) + '] { ' + pstr + ' }\n'
        return dstr

    def parse_mounts(self):
        mnts = []
        f = open('/proc/mounts', 'r')
        for line in f:
            m = re.match('^([^\s]+)\s+([^\s]+)\s+([^\s]+)\s+([^\s]+)', line)
            if m:
                mnts.append({'dev': m.group(1), 'mnt': m.group(2), 'type': m.group(3),
                             'opts': m.group(4)})
        f.close()
        return mnts

    def cgroup_types(self):
        return frozenset(['cpu', 'cpuset', 'memory', 'devices', 'freezer',
                          'blkio', 'cpuacct'])

    def find_cgroup_mntpoint(self, mnts):
        cgmlst = []
        for mnt in mnts:
            if mnt['dev'] == 'cgroup':
                cgmlst.append(mnt)
        return cgmlst

    def is_mounted(self, mnts, mtype, mdev, mloc):
        for mnt in mnts:
            if ((not mloc or mnt['mnt'] == mloc) and
                (not mtype or mnt['type'] == mtype) and
                (not mdev or mnt['dev'] == mdev)):
                return True
        return False

    def mount_cgroup(self):
        if not self.cgroup_mntslist:
            mnts = self.parse_mounts()
            mroot = '/sys/fs/cgroup'
            self.dbg_print(2, 'Mounting cgroup on ' + mroot)
            if not self.is_mounted(mnts, 'tmpfs', None, mroot):
                self.exec_cmd(['mount', '-t', 'tmpfs', 'cgroup_root', mroot],
                              what = ('Failed to mount tmpfs on ' + mroot))
            for ss in self.cgroup_types():
                cpath = os.path.join(mroot, ss)
                self.dbg_print(2, 'Mounting cgroup:' + ss + ' on ' + cpath)
                if not os.path.isdir(cpath):
                    os.makedirs(cpath)
                opts = 'rw,relatime,' + ss
                if not self.is_mounted(mnts, 'cgroup', None, cpath):
                    self.exec_cmd(['mount', '-t', 'cgroup', '-o', opts, ss,
                                   cpath],
                                  what = ('Failed to mount cgroup on ' + cpath))
                self.cgroup_mntslist.append({'dev': 'cgroup', 'mnt': cpath,
                                            'type': 'cgroup', 'opts': opts})
        return self.cgroup_mntslist

    def cgroup_setup_cpuset(self, path, cpus):
        if not os.path.isdir(path):
            os.makedirs(path)
        clist = self.mk_cpu_list(cpus)
        self.write_file(os.path.join(path, 'cpuset.cpus'), clist)
        self.write_file(os.path.join(path, 'cpuset.cpu_exclusive'), '1')

    def move_tasks_to_cpuset(self, csmnt, cset, plist):
        tpath = os.path.join(csmnt, cset, 'tasks')
        failed = []
        for pid in plist:
            try:
                self.write_file(tpath, str(pid))
            except:
                self.dbg_print(2, 'Failed to move task ' + str(pid) +
                               ' on cpuset ' + cset)
                failed.append(pid)
        return failed

    def find_cgroup_cpuset_mnt(self):
        for mnt in self.cgroup_mntslist:
            opts = frozenset(mnt['opts'].split(','))
            if 'cpuset' in opts:
                return mnt
        return None

    def cgroup_setup(self, iocpus, llcpus):
        self.mount_cgroup()
        cpuset_mnt = self.find_cgroup_cpuset_mnt()
        if not cpuset_mnt:
            self.dbg_print(0, 'Unable to find cgroup cpuset mount point')
            exit(2)
        self.cgroup_setup_cpuset(os.path.join(cpuset_mnt['mnt'],
                                              self.io_cpuset),
                                 iocpus)
        self.cgroup_setup_cpuset(os.path.join(cpuset_mnt['mnt'],
                                              self.ll_cpuset),
                                 llcpus)
        self.move_tasks_to_cpuset(cpuset_mnt['mnt'], self.io_cpuset,
                                  self.find_user_tasks())

    def run_lowlat_task(self, args):
        cpuset_mnt = self.find_cgroup_cpuset_mnt()
        if not cpuset_mnt:
            self.dbg_print(0, 'Unable to find cgroup cpuset mount point. '
                           'Run -s to setup.')
            exit(2)
        cpid = os.fork()
        if cpid == 0:
            os.execvp(args[0], args)
            os._exit(17)
        self.move_tasks_to_cpuset(cpuset_mnt['mnt'], self.ll_cpuset, [cpid])
        xstatus = None
        while True:
            wstat = os.waitpid(cpid, 0)
            if wstat[0] == cpid:
                xstatus = wstat[1]
                break
        return os.WEXITSTATUS(xstatus)

def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument('-s', '--setup', type=bool, default=False,
                        help='Sets up the environment')
    parser.add_argument('-c', '--cmd', type=str,
                        help='Execute a binary with its arguments')
    parser.add_argument('-L', '--log_level', type=int, default=5,
                        help='Sets the logging level')
    parser.add_argument('rargs', nargs=argparse.REMAINDER)
    args = parser.parse_args()

    llc = llctx(dbg_level = args.log_level)

    if args.setup:
        llc.setup()
    if args.cmd == 'run':
        bl = llc.read_interrupts()
        llc.run_lowlat_task(args.rargs)
        al = llc.read_interrupts()
        print llc.diff_interrupts(bl, al)


if __name__ == '__main__':
    main()
