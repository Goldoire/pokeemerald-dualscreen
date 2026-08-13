#!/usr/bin/env python3
"""Asset-hole tool: makes a distributable libmain.so with no game assets.

Every read-only-data symbol in the built library whose bytes appear
verbatim in the retail Emerald ROM (the decomp is byte-matching, so real
assets do) is recorded in a manifest and zeroed in the output library.
At first launch the app restores those ranges from the user's own ROM,
which reproduces the original library exactly.

Usage:
  make_asset_holes.py analyze <libmain.so> <rom.gba>  (rom is always argv[3])
  make_asset_holes.py build <unstripped.so> <rom.gba> <in.so> <out.so> <manifest.bin>

Manifest format (little endian): u32 count, then per entry
u32 vaddr, u32 size, u32 romOffset. Prefixed with the ROM's SHA-1 (20 bytes).
"""
import hashlib
import struct
import sys

from elftools.elf.elffile import ELFFile

MIN_SIZE = 32


def collect_entries(so_path, rom):
    entries = []
    matched = unmatched = 0
    with open(so_path, "rb") as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            sys.exit("no .symtab: use the unstripped libmain.so")
        rodata = [s for s in elf.iter_sections()
                  if s.name in (".rodata", ".data.rel.ro", ".data", "script_data")]
        ranges = [(s["sh_addr"], s["sh_addr"] + s["sh_size"], s) for s in rodata]

        def section_for(addr):
            for lo, hi, s in ranges:
                if lo <= addr < hi:
                    return lo, hi, s
            return None, None, None

        # Some data comes from assembly objects whose symbols carry no size;
        # derive effective sizes from the distance to the next symbol.
        addr_syms = []
        for sym in symtab.iter_symbols():
            addr = sym["st_value"]
            lo, hi, section = section_for(addr)
            if section is not None:
                addr_syms.append((addr, sym["st_size"], hi))
        addr_syms.sort()
        candidates = []
        for i, (addr, size, section_end) in enumerate(addr_syms):
            if size == 0:
                nxt = section_end
                for j in range(i + 1, len(addr_syms)):
                    if addr_syms[j][0] > addr:
                        nxt = min(addr_syms[j][0], section_end)
                        break
                size = nxt - addr
            if size >= MIN_SIZE:
                candidates.append((addr, size))
        # Drop candidates fully contained in a previous one (aliases).
        candidates.sort()
        pruned = []
        for addr, size in candidates:
            if pruned and addr + size <= pruned[-1][0] + pruned[-1][1]:
                continue
            pruned.append((addr, size))

        # ARM32 uses REL relocations with in-place addends: any relocation
        # target word in .data holds a link-time address the loader rewrites,
        # so it must never be zeroed or filled from ROM. Split candidate
        # ranges around every relocation target.
        reloc_offsets = []
        for relsec_name in (".rel.dyn", ".rela.dyn"):
            relsec = elf.get_section_by_name(relsec_name)
            if relsec is not None:
                reloc_offsets.extend(r["r_offset"] for r in relsec.iter_relocations())
        reloc_offsets.sort()
        import bisect

        def split_around_relocs(addr, size):
            parts = []
            start = addr
            end = addr + size
            i = bisect.bisect_left(reloc_offsets, start - 3)
            while i < len(reloc_offsets) and reloc_offsets[i] < end:
                r = reloc_offsets[i]
                if r + 4 > start:
                    if r > start:
                        parts.append((start, r - start))
                    start = r + 4
                i += 1
            if end > start:
                parts.append((start, end - start))
            return [(a, s) for a, s in parts if s >= MIN_SIZE]

        pruned = [part for addr, size in pruned
                  for part in split_around_relocs(addr, size)]

        def match_range(data, addr):
            # Whole-range match, else strip alignment padding, else split:
            # derived ranges often span several objects and padding gaps.
            rom_off = rom.find(data)
            if rom_off >= 0:
                return [(addr, len(data), rom_off)]
            stripped = data.rstrip(b"\x00")
            if MIN_SIZE <= len(stripped) < len(data):
                rom_off = rom.find(stripped)
                if rom_off >= 0:
                    return [(addr, len(stripped), rom_off)]
            if len(data) < MIN_SIZE * 2:
                return []
            mid = (len(data) // 2) & ~3
            return match_range(data[:mid], addr) + match_range(data[mid:], addr + mid)

        for addr, size in pruned:
            lo, hi, section = section_for(addr)
            data = section.data()[addr - lo : addr - lo + size]
            if len(data) != size:
                continue
            found = match_range(data, addr)
            for faddr, fsize, frow in found:
                entries.append((faddr, fsize, frow))
                matched += fsize
            unmatched += size - sum(f[1] for f in found)
    entries.sort()
    # Merge adjacent/overlapping ranges with contiguous ROM offsets.
    merged = []
    for addr, size, rom_off in entries:
        if merged:
            paddr, psize, prom = merged[-1]
            if addr <= paddr + psize and rom_off == prom + (addr - paddr):
                merged[-1] = (paddr, max(paddr + psize, addr + size) - paddr, prom)
                continue
        merged.append((addr, size, rom_off))
    return merged, matched, unmatched


def vaddr_to_file_offset(elf, vaddr):
    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD":
            continue
        if seg["p_vaddr"] <= vaddr < seg["p_vaddr"] + seg["p_filesz"]:
            return vaddr - seg["p_vaddr"] + seg["p_offset"]
    return None


def main():
    mode = sys.argv[1]
    rom = open(sys.argv[3], "rb").read()

    entries, matched, unmatched = collect_entries(sys.argv[2], rom)
    total = matched + unmatched
    print(f"symbols matched into ROM: {matched/1e6:.1f} MB "
          f"({matched * 100 // max(total, 1)}% of {total/1e6:.1f} MB candidate rodata)")
    print(f"manifest entries after merge: {len(entries)}")

    if mode == "analyze":
        return

    _, rom_path, in_path, out_path, manifest_path = sys.argv[1:6] and sys.argv[2:7]
    data = bytearray(open(in_path, "rb").read())
    with open(in_path, "rb") as f:
        elf = ELFFile(f)
        zeroed = 0
        for vaddr, size, _ in entries:
            off = vaddr_to_file_offset(elf, vaddr)
            if off is None:
                sys.exit(f"vaddr {vaddr:#x} not in a PT_LOAD segment")
            data[off : off + size] = bytes(size)
            zeroed += size
    open(out_path, "wb").write(data)

    with open(manifest_path, "wb") as m:
        m.write(hashlib.sha1(rom).digest())
        m.write(struct.pack("<I", len(entries)))
        for vaddr, size, rom_off in entries:
            m.write(struct.pack("<III", vaddr, size, rom_off))
    print(f"zeroed {zeroed/1e6:.1f} MB -> {out_path}")
    print(f"manifest -> {manifest_path}")


if __name__ == "__main__":
    main()
