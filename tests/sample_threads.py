#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Samples a process for N seconds: prints "<wakeups per second> <cpu percent of one core>" summed over its threads."""
import os
import sys
import time

pid, secs = int(sys.argv[1]), float(sys.argv[2])
hz = os.sysconf("SC_CLK_TCK")


def snap():
    ticks = ctx = 0
    for t in os.listdir("/proc/%d/task" % pid):
        try:
            st = open("/proc/%d/task/%s/stat" % (pid, t)).read()
            f = st[st.rindex(")") + 2:].split()
            ticks += int(f[11]) + int(f[12])
            for line in open("/proc/%d/task/%s/status" % (pid, t)):
                if line.startswith("voluntary_ctxt_switches"):
                    ctx += int(line.split()[1])
        except OSError:
            pass
    return ticks, ctx


a, t0 = snap(), time.time()
time.sleep(secs)
b, dt = snap(), time.time() - t0
print("%.1f %.2f" % ((b[1] - a[1]) / dt, (b[0] - a[0]) / hz / dt * 100))
