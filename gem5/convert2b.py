import sys
import struct

try:
    infile = sys.argv[1]
    outfile = sys.argv[2]
except IndexError:
    print("usage: python3 convert2b.py </path/to/inputfile> <path/to/outputfile>")
    exit(-1)

f = open(infile, 'rb')

# Buncha masks n shifts
# Why are we doing this in python? I will never know.
OPCODE_MASK = 0x1F # Mask first 5 bits
OPCODE_SHIFT = 2
# Don't need any of these ones but I did it while determining the other mask
RD_RS_MASK = 0x1F
RD_SHIFT = 7
FUNCT3_MASK = 0x7
FUNCT3_SHIFT = 12
RS1_SHIFT = 15
RS2_SHIFT = 20
# We do edit this one
FUNCT7_MASK = 0x7F
FUNCT7_SHIFT = 25

# Opcode for R-Type instructions is 0x0c
# Y'all should put this in the course resources
# www.cs.sfu.ca/~ashriram/Courses/CS295/assets/notebooks/RISCV/RISCV_CARD.pdf :) 

# So ACTUALLY for SOME REASON gem5's RISC-V ISA format uses an opcode-5 field? WTF???
# But for that it's 0x0c
# 0x0e actually unused I'm just lazy
R_TYPE_OPCODES = {0x0c, 0x0e}
# MUL/DIV type instructions use funct7 = 0x1, MULB/DIVB use funct7 = 0x2
MUL_TYPE_FUNCT7 = 0x1
MULB_FUNCT3 = 0x0 # same
DIVB_FUNCT3 = 0x4
MULB_TYPE_FUNCT7 = 0x2

# Ripped from objdump
MAIN_START = 0x1ec
MAIN_END = 0x2f4

def convert(instr):
    opcode = (instr >> OPCODE_SHIFT) & OPCODE_MASK
    if opcode not in R_TYPE_OPCODES:
        return instr
    funct7 = (instr >> FUNCT7_SHIFT) & FUNCT7_MASK
    if funct7 != MUL_TYPE_FUNCT7:
        return instr
    funct3 = (instr >> FUNCT3_SHIFT) & FUNCT3_MASK
    if funct3 not in (MULB_FUNCT3, DIVB_FUNCT3):
        return instr
    return (instr & ~(FUNCT7_MASK << FUNCT7_SHIFT)) | (MULB_TYPE_FUNCT7 << FUNCT7_SHIFT)

with open(infile, 'rb') as f:
    data = bytearray(f.read())

count = 0
off = MAIN_START
while off + 4 <= MAIN_END:
    instr = struct.unpack_from('<I', data, off)[0]
    new_instr = convert(instr)
    if new_instr != instr:
        print(f"Converting at file offset 0x{off:x}: 0x{instr:08x} -> 0x{new_instr:08x}")
        struct.pack_into('<I', data, off, new_instr)
        count += 1
    hw = struct.unpack_from('<H', data, off)[0]
    if (hw & 0x3) == 0x3:
        off += 4
    else:
        off += 2

print(f"Converted {count} instructions")

with open(outfile, 'wb') as out:
    out.write(data)

