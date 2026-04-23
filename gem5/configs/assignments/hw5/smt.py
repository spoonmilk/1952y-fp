"""
This is the RISCV equivalent to `simple.py` (which is designed to run using the
X86 ISA). More detailed documentation can be found in `simple.py`.
"""

# import m5
# from m5.objects import *
# from m5.objects.RiscvMMU import RiscvMMU
# from m5.objects.RiscvCPU import RiscvCPU
# from m5.objects.BaseO3CPU import BaseO3CPU
# from m5.objects.BranchPredictor import *

# m5.util.addToPath("../../")
# from common import SimpleOpts

# SimpleOpts.add_option("binary1", type=str)
# SimpleOpts.add_option("binary2", type=str)
# args = SimpleOpts.parse_args()

# thispath = os.path.dirname(os.path.realpath(__file__))
# bin1_path = os.path.join(
#     thispath,
#     "../../..",
#     args.binary1
# )

# bin2_path = os.path.join(
#     thispath,
#     "../../..",
#     args.binary2
# )

# system = System()

# system.multi_thread = True

# system.clk_domain = SrcClockDomain()
# system.clk_domain.clock = "1GHz"
# system.clk_domain.voltage_domain = VoltageDomain()

# system.mem_mode = "timing"
# system.mem_ranges = [AddrRange("8192MB")]
# system.cpu = RiscvO3CPU(numThreads=2)

# system.membus = SystemXBar()

# system.cpu.icache_port = system.membus.cpu_side_ports
# system.cpu.dcache_port = system.membus.cpu_side_ports

# system.cpu.createInterruptController()

# system.mem_ctrl = MemCtrl()
# system.mem_ctrl.dram = DDR3_1600_8x8()
# system.mem_ctrl.dram.range = system.mem_ranges[0]
# system.mem_ctrl.port = system.membus.mem_side_ports

# system.system_port = system.membus.cpu_side_ports

# system.workload = SEWorkload.init_compatible(bin1_path)

# p0 = Process()
# p0.cmd = [bin1_path]
# p1 = Process(pid=p0.pid + 1)
# p1.cmd = [bin2_path]

# system.cpu.workload = [p0, p1]
# system.cpu.createThreads()

# root = Root(full_system=False, system=system)
# m5.instantiate()

# print(f"Beginning simulation!")
# exit_event = m5.simulate()
# print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")


import m5
from m5.objects import *
from m5.objects.RiscvMMU import RiscvMMU
from m5.objects.RiscvCPU import RiscvCPU
from m5.objects.BaseO3CPU import BaseO3CPU
from m5.objects.BranchPredictor import *

m5.util.addToPath("../../")
from common import SimpleOpts

SimpleOpts.add_option("binary1", type=str)
SimpleOpts.add_option("binary2", type=str)
args = SimpleOpts.parse_args()

thispath = os.path.dirname(os.path.realpath(__file__))
bin1_path = os.path.join(
    thispath,
    "../../..",
    args.binary1
)

bin2_path = os.path.join(
    thispath,
    "../../..",
    args.binary2
)

system = System()

system.multi_thread = True

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange("8192MB")]
system.cpu = RiscvO3CPU(numThreads=2)

system.membus = SystemXBar()

# --- L1 Caches ---
system.cpu.icache = Cache(
    size="32kB",
    assoc=8,
    tag_latency=1,
    data_latency=1,
    response_latency=1,
    mshrs=4,
    tgts_per_mshr=20
)
system.cpu.dcache = Cache(
    size="32kB",
    assoc=8,
    tag_latency=1,
    data_latency=1,
    response_latency=1,
    mshrs=4,
    tgts_per_mshr=20
)

# Connect caches between CPU and memory bus
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

system.workload = SEWorkload.init_compatible(bin1_path)

p0 = Process()
p0.cmd = [bin1_path]
p1 = Process(pid=p0.pid + 1)
p1.cmd = [bin2_path]

system.cpu.workload = [p0, p1]
system.cpu.createThreads()

# CHAOSCache fault injector
# system.chaos = CHAOSCache(
#     target_cache=system.cpu.dcache,
#     probability=0.001,
#     faultType="bit_flip",
#     bitsToChange=1,
#     corruptionSize=1
# )

root = Root(full_system=False, system=system)
m5.instantiate()

print(f"Beginning simulation!")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")