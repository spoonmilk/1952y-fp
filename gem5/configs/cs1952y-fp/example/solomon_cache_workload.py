#     gem5 solomon_cache_workload.py --binary <path> [--symbol-errors N]
#                                    [--chaos-prob P] [--chaos-bits B]

import m5
from m5.objects import *
from m5.objects.RiscvMMU import RiscvMMU
from m5.objects.RiscvCPU import RiscvCPU
from m5.objects.BaseO3CPU import BaseO3CPU
from m5.objects.BranchPredictor import *

m5.util.addToPath("../../")
from common import SimpleOpts

SimpleOpts.add_option("binary", type=str)
SimpleOpts.add_option(
    "--symbol-errors", type=int, default=4,
    help="Symbol errors correctable per cache block (default 4)"
)
SimpleOpts.add_option(
    "--chaos-prob", type=float, default=0.0001,
    help="Bit-flip probability for CHAOS injector (default 0.0001)"
)
SimpleOpts.add_option(
    "--chaos-bits", type=int, default=1,
    help="Bits flipped per CHAOS corruption event (default 1)"
)
SimpleOpts.add_option(
    "--delay", type=int, default=52077000,
    help="Delay for CHAOS corruption event (default 52077000)"
)
SimpleOpts.add_option(
    "--scrub-interval", type=int, default=10,
    help="Cycles between scrub passes (default 10)"
)
SimpleOpts.add_option(
    "--scrub-tighten-factor", type=float, default=2.0,
    help="Factor to tighten scrub interval by (default 2.0)"
)
SimpleOpts.add_option(
    "--scrub-relax-factor", type=float, default=2.0,
    help="Factor to relax scrub interval by (default 2.0)"
)
args = SimpleOpts.parse_args()

thispath = os.path.dirname(os.path.realpath(__file__))
bin_path = os.path.join(thispath, "../../..", args.binary)

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange("8192MB")]

system.cpu = RiscvO3CPU()

system.membus = SystemXBar()

cache_params = dict(
    size="32kB",
    assoc=8,
    tag_latency=1,
    data_latency=1,
    response_latency=1,
    mshrs=1,
    tgts_per_mshr=1,
    scrub_interval_cycles=args.scrub_interval,
    symbol_errors=args.symbol_errors,
    correction_grace_ticks=args.delay,
    scrub_tighten_factor=args.scrub_tighten_factor,
    scrub_relax_factor=args.scrub_relax_factor
)

#system.cpu.icache = SolomonCache(**cache_params)
system.cpu.icache = Cache(
    size="32kB",
    assoc=8,
    tag_latency=1,
    data_latency=1,
    response_latency=1,
    mshrs=4,
    tgts_per_mshr=20
)
system.cpu.dcache = SolomonCache(**cache_params)

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

# CHAOS fault injector — targets dcache only so icache stays clean
system.chaos = CHAOSCache(
    target_cache=system.cpu.dcache,
    firstClock=args.delay / 1000,  # tick-to-clock ratio is 1000
    probability=args.chaos_prob,
    faultType="bit_flip",
    bitsToChange=args.chaos_bits,
    corruptionSize=1,
)

root = Root(full_system=False, system=system)
m5.instantiate()

print(
    f"Simulating SolomonCache Workload:"
    f"symbol_errors={args.symbol_errors}, "
    f"chaos_prob={args.chaos_prob}, "
    f"chaos_bits={args.chaos_bits}"
)
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")
