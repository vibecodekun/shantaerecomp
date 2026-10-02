// Ghidra's listing of ROM ranges, read-only (tools/ghidra_listing.py runs it headless).
// Arguments: BANK:START:END in hex, e.g. 6:4AA7:4B19. Walks instruction by instruction
// with the pseudo-disassembler, so nothing is written to the program and data between
// routines does not confuse later ranges, and steps over the address and bank that
// follow CALL $0540 / $056B / $0571 (the far call and far jumps).
import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSpace;
import ghidra.program.model.mem.Memory;

public class Listing extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        PseudoDisassembler dis = new PseudoDisassembler(currentProgram);
        for (String arg : getScriptArgs()) {
            String[] p = arg.split(":");
            int bank = Integer.parseInt(p[0], 16);
            long pc = Long.parseLong(p[1], 16), end = Long.parseLong(p[2], 16);
            // GhidraBoy maps bank 0 in the default space and bank N as the overlay "romN" (decimal).
            AddressSpace space = bank == 0 || pc < 0x4000
                    ? currentProgram.getAddressFactory().getDefaultAddressSpace()
                    : currentProgram.getAddressFactory().getAddressSpace("rom" + bank);
            println(String.format("RANGE %02X:%04X-%04X", bank, pc, end));
            while (pc < end) {
                Address at = space.getAddress(pc);
                PseudoInstruction ins = dis.disassemble(at);
                if (ins == null) {
                    println(String.format("%02X:%04X  %02x       ??", bank, pc, mem.getByte(at) & 0xFF));
                    pc += 1;
                    continue;
                }
                int length = ins.getLength();
                StringBuilder raw = new StringBuilder();
                for (int i = 0; i < length; i++) raw.append(String.format("%02x", mem.getByte(at.add(i)) & 0xFF));
                String text = ins.toString();
                println(String.format("%02X:%04X  %-8s %s", bank, pc, raw, text));
                pc += length;
                String flat = text.replace(" ", "").toUpperCase();
                if (flat.equals("CALL0X0540") || flat.equals("CALL0X056B") || flat.equals("CALL0X0571")) {
                    Address data = space.getAddress(pc);
                    int lo = mem.getByte(data) & 0xFF, hi = mem.getByte(data.add(1)) & 0xFF,
                        far = mem.getByte(data.add(2)) & 0xFF;
                    println(String.format("%02X:%04X  %02x%02x%02x   <far %02X:%04X>", bank, pc, lo, hi, far, far,
                            lo | hi << 8));
                    pc += 3;
                }
            }
        }
    }
}
