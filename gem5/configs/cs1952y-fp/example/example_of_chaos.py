"""
Took smt.py and just added in chaos to make sure it works.
Single-process version.
"""


import m5
from m5.objects import *
from m5.objects.RiscvMMU import RiscvMMU
from m5.objects.RiscvCPU import RiscvCPU
from m5.objects.BaseO3CPU import BaseO3CPU
from m5.objects.BranchPredictor import *

m5.util.addToPath("../../")
from common import SimpleOpts

SimpleOpts.add_option("binary", type=str)
args = SimpleOpts.parse_args()

thispath = os.path.dirname(os.path.realpath(__file__))
bin_path = os.path.join(
    thispath,
    "../../..",
    args.binary
)

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange("8192MB")]

system.cpu = RiscvO3CPU()

system.membus = SystemXBar()

# L1 Caches 
system.cpu.icache = HammingCache(
    size="32kB",
    assoc=8,
    tag_latency=1,
    data_latency=1,
    response_latency=1,
    mshrs=4,
    tgts_per_mshr=20
)
system.cpu.dcache = HammingCache(
    size="32kB",
    assoc=8,
    tag_latency=1,
    data_latency=1,
    response_latency=1,
    mshrs=4,
    tgts_per_mshr=20
)

# connect caches between CPU and memory bus
system.cpu.icache_port = system.cpu.icache.cpu_side
system.cpu.dcache_port = system.cpu.dcache.cpu_side

system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.dcache.mem_side = system.membus.cpu_side_ports

system.cpu.createInterruptController()

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

system.workload = SEWorkload.init_compatible(bin_path)

process = Process()
process.cmd = [bin_path]
system.cpu.workload = process
system.cpu.createThreads()

#CHAOSCache fault injector
# seems like the probability should not go over 0.0001 because otherwise the system will just crash immediately, but this is something we can play around with
system.chaos = CHAOSCache(
    target_cache=system.cpu.dcache,
    probability=0.0001,
    faultType="bit_flip",
    bitsToChange=1,
    corruptionSize=1
)

root = Root(full_system=False, system=system)
m5.instantiate()

print(f"Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")