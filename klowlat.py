#!/usr/bin/python
# Local Variables:
# mode: python
# End:
#
# Kernel cmdline: isolcpus=CPU_LIST nohz_full=CPU_LIST nowatchdog
#
# Example: isolcpus=8-47 nohz_full=8-47 nowatchdog
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
        self.llcpus = self.get_ll_cpus(self.cmdline, self.num_cpus)
        self.iocpus = self.sub_cpus(range(0, self.num_cpus), self.llcpus)

    def setup(self):
        self.cycle_offline_cpus(self.llcpus)
        self.setup_machine_check_polls(self.llcpus, self.mcp_interval)
        self.relocate_rcu_tasks(self.iocpus)
        self.relocate_irqs(self.iocpus)

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

    def get_ll_cpus(self, cmdln, ncpus):
        llcpus = []
        m = re.search('isolcpus=([^\s]+)', cmdln)
        if not m:
            self.dbg_print(0, 'Missing isolcpus config in kernel '
                           'command line: ' + cmdln)
            exit(2)
        for tok in m.group(1).split(','):
            mx = re.match('^(\d+)$', tok)
            if mx:
                llcpus.append(int(mx.group(1)))
            else:
                mx = re.match('^(\d+)-(\d+)$', tok)
                if mx:
                    llcpus.extend(range(int(mx.group(1)),
                                        int(mx.group(2)) + 1))
        return llcpus

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

    def create_cpu_list_str(self, cpus):
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
        clist = self.create_cpu_list_str(cpus)
        self.exec_cmd(['taskset', '-pc', clist, str(pid)],
                      what = 'Failed to set task affinity')

    def set_offline_cpu(self, cpu, on):
        self.write_file(os.path.join('/sys/devices/system/cpu', 'cpu' + str(cpu),
                                     'online'), str(on))

    def cycle_offline_cpus(self, cpus):
        for cpu in cpus:
            self.set_offline_cpu(cpu, 0)
        for cpu in cpus:
            self.set_offline_cpu(cpu, 1)

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
        clist = self.create_cpu_list_str(cpus)
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

    def run_lowlat_task(self, args):
        cpid = os.fork()
        if cpid == 0:
            self.set_task_affinity(os.getpid(), self.llcpus)
            os.execvp(args[0], args)
            self.dbg_print(0, 'Failed to execute: ' + str(args))
            os._exit(17)
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
    parser.add_argument('-s', '--setup', default=False, action='store_true',
                        help='Sets up the environment')
    parser.add_argument('-c', '--cmd', type=str,
                        help='Specify the command to run')
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
