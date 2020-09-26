# Copyright (c) 2012-2013 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import print_function
from __future__ import absolute_import

# import the m5 (gem5) library created when gem5 is built
import m5
# import all of the SimObjects
from m5.objects import *
# import emulation primitives
m5.util.addToPath('../sim/inc/')
# import model defines
import defs
# import model parameters
import params
# extract as local variables
for define in dir(params):
    if define[0] != '_':        # This filters out the swig stuff.
        exec("val = params." + define)
        exec(define + " = int(val) & 0xFFFFFFFF")

# Add the common scripts to our path
m5.util.addToPath('../../gem5/configs/')

# import the SimpleOpts module
from common import SimpleOpts

# Set the usage message to display
SimpleOpts.set_usage("usage: %prog [options] <binary to execute> <trace replay mode - 1 / 0>")

# Finalize the arguments and grab the opts so we can pass it on to our objects
(opts, args) = SimpleOpts.parse_args()

# get ISA for the default binary to run. This is mostly for simple testing
isa = str(m5.defines.buildEnv['TARGET_ISA']).lower()

# Default to running 'hello', use the compiled ISA to find the binary
# grab the specific path to the binary
thispath = os.path.dirname(os.path.realpath(__file__))
binary = os.path.join(thispath, '../../',
                      'gem5/tests/test-progs/hello/bin/', isa, 'linux/hello')

# Check if there was a binary passed in via the command line and error if
# there are too many arguments
if len(args) == 2:
    binary = args[0]
    tre_en = int(args[1])
elif len(args) > 2:
    SimpleOpts.print_help()
    m5.fatal("Expected binary_name and trace_replay_mode as positional arguments")

# create the system we are going to simulate
system = System()

# Set the clock fequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = str(CLOCK_SPEED_GHZ) + 'GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = 'timing'               # Use timing accesses
mem_ranges = [AddrRange(0, size=0xE0100000)]
# system.mem_ranges = [AddrRange('8GB')] # Create an address range
system.mem_ranges = mem_ranges

# Create a simple CPU
if tre_en:
    system.mgr = TRE(
        id=0,   # This must correspond to the ID assigned in the user spec file.
        queue_depth=WQ_DEPTH,
        max_outstanding_addrs=MAX_OUTSTANDING_REQS
    )
else:
    system.mgr = TimingSimpleCPU()
    # Create the interrupt controller for the CPU
    system.mgr.createInterruptController()
    system.mgr_icache = Cache(size="4kB", assoc=2, tag_latency=1, data_latency=1, mshrs=4,
                          response_latency=1, tgts_per_mshr=4, addr_ranges=mem_ranges)

wrkr = []; wrkr_icache = []; wq = []
for i in range(NUM_WORKER):
    if tre_en:
        wrkr.append(TRE(
            id=i+1, # This must correspond to the ID assigned in the user spec file.
            max_outstanding_addrs=MAX_OUTSTANDING_REQS
        ))
    else:
        wrkr.append(TimingSimpleCPU())
        wrkr[i].createInterruptController()

        wrkr_icache.append(Cache(size="4kB", assoc=2, tag_latency=1, data_latency=1, mshrs=4,
                               response_latency=1, tgts_per_mshr=4, addr_ranges=mem_ranges))

    pop_addr  = WQ_POP_ADDR
    push_addr = WQ_PUSH_BASE_ADDR + 4 * (i + 1)

    wq.append(WorkQueue(
        push_addr = Addr(push_addr),
        pop_addr = Addr(pop_addr),
        range = AddrRange(pop_addr, size = 4 * (1 + NUM_WORKER)),
        latency = '1ns', size = WQ_DEPTH))

    setattr(system, 'wrkr%u' % i, wrkr[i])
    setattr(system, 'wq%u' % i, wq[i])

if not tre_en:
    system.wrkr_icache = wrkr_icache
# system.work_queue = wq

# Create a memory bus
system.combus = NoncoherentXBar(forward_latency=1, frontend_latency=1, response_latency=1, width=4)
system.dbus   = NoncoherentXBar(forward_latency=1, frontend_latency=1, response_latency=1,
                             width=4)
if not tre_en:
    system.ibus   = NoncoherentXBar(forward_latency=1, frontend_latency=1, response_latency=1,
                                 width=4)
system.membus = SystemXBar()
dmux = []
for i in range(NUM_WORKER):
    dmux.append(NoncoherentXBar(forward_latency=0, frontend_latency=0, response_latency=0, width=4))
    setattr(system, 'dmux%u' % i, dmux[i])

# Create an L1 instruction and data cache
system.dcache = Cache(size="4kB", assoc=2, tag_latency=1, data_latency=1, mshrs=4,
                      response_latency=1, tgts_per_mshr=4, addr_ranges=mem_ranges)
system.dspm   = SimpleMemory(range=AddrRange(SPM_BASE_ADDR, size=str(SPM_SIZE_BYTES) + "B"),
                             in_addr_map=False, latency='1ns')

# Connect the instruction, data caches and scratchpad to the PEs.
system.dcache.mem_side = system.membus.slave

system.dcache.cpu_side = system.dbus.master
if not tre_en:
    system.ibus.master = system.membus.slave

system.dspm.port = system.dbus.master

if not tre_en:
    system.mgr.icache_port = system.mgr_icache.cpu_side
    system.mgr_icache.mem_side = system.ibus.slave
    system.mgr.dcache_port = system.combus.slave
else:
    system.mgr.port = system.combus.slave

for i in range(NUM_WORKER):
    if not tre_en:
        wrkr[i].icache_port = system.wrkr_icache[i].cpu_side
        system.wrkr_icache[i].mem_side = system.ibus.slave
        wrkr[i].dcache_port = dmux[i].slave
    else:
        wrkr[i].port = dmux[i].slave
    dmux[i].master = system.dbus.slave
    dmux[i].master = wq[i].pop_port
    system.combus.master = wq[i].push_port
system.combus.master = system.dbus.slave

# Connect the system up to the membus.
system.system_port = system.membus.slave

# Create a DDR3 memory controller.
system.mem_ctrl = DDR3_1600_8x8()
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.master

# Share the PE clock and voltage domains with the system.
for i in range(NUM_WORKER):
    wrkr[i].clk_domain = system.clk_domain

# Create a process for the user application.
if not tre_en:
    process = Process()
    process.reserved_start_addr = WQ_POP_ADDR
    process.reserved_size = 2*4096    # Must be a multiple of page size (4kB).

    # Set the command
    # cmd is a list which begins with the executable (like argv)
    process.cmd = [binary]
    # Set the cpu to use the process as its workload and create thread contexts
    system.mgr.workload = process
    system.mgr.createThreads()
    for i in range(NUM_WORKER):
        wrkr[i].workload = process
        wrkr[i].createThreads()

# set up the root SimObject and start the simulation
root = Root(full_system = False, system = system)
# instantiate all of the objects we've created above
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print('Exiting @ tick %i because %s' % (m5.curTick(), exit_event.getCause()))
