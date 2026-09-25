// Small address-preserving ROM inspection utility using the recompiler decoder.
#include "recompiler/decoder.h"
#include "recompiler/rom.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: disasm_range ROM bank start end (hex)\n");
        return 1;
    }
    auto rom = gbrecomp::ROM::load(argv[1]);
    if (!rom) return 1;
    unsigned bank = std::strtoul(argv[2], nullptr, 16);
    unsigned pc = std::strtoul(argv[3], nullptr, 16);
    unsigned end = std::strtoul(argv[4], nullptr, 16);
    if (bank >= rom->bank_count() || pc >= end || end > 0x8000) return 1;
    gbrecomp::Decoder decoder(*rom);
    while (pc < end) {
        auto instruction = decoder.decode(static_cast<uint16_t>(pc), static_cast<uint8_t>(bank));
        std::printf("%02X:%04X  %-24s\n", bank, pc, instruction.disassemble().c_str());
        if (!instruction.length) return 1;
        pc += instruction.length;
    }
}
