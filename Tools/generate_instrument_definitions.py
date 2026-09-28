"""Combine an MU QWS definition with the VL/PVL/SG banks from S-YXG2026."""

import argparse
import csv
from pathlib import Path


VL_BANKS = {4224, 4225, 12544}
BANK_NAMES = {
    0: "MU2000 main voices",
    2048: "MU2000 sampling voices",
    4224: "VL",
    4225: "PVL",
    12544: "SG singing voices",
}


def parse_row(line: str):
    fields = next(csv.reader([line]))
    if len(fields) < 4 or fields[0].strip() != "-1":
        return None
    program = int(fields[1].strip())
    bank = int(fields[2].strip())
    name = ",".join(fields[3:]).strip()
    if not 0 <= program < 128 or not 0 <= bank < 16384 or not name:
        raise ValueError(f"Invalid QWS row: {line}")
    return program, bank, name


def read_lines(path: Path):
    return path.read_text(encoding="ascii").splitlines()


def combine(mu_lines, hybrid_lines):
    added = {}
    for line in hybrid_lines:
        row = parse_row(line)
        if row and row[1] in VL_BANKS:
            key = row[:2]
            if key in added:
                raise ValueError(f"Duplicate VL/PVL/SG voice: {key}")
            added[key] = row[2]
    if len(added) != 328:
        raise ValueError(f"Expected 328 VL/PVL/SG voices, found {len(added)}")

    start = mu_lines.index("; Normal voices")
    end = mu_lines.index("; Model Exclusive voices")
    normal = mu_lines[start + 1:end]
    last_index = {}
    existing = set()
    for index, line in enumerate(normal):
        row = parse_row(line)
        if row:
            last_index[row[0]] = index
            existing.add(row[:2])
    if len(last_index) != 128 or existing.intersection(added):
        raise ValueError("MU normal voices are incomplete or conflict with VL")

    result = mu_lines[:start + 1]
    for index, line in enumerate(normal):
        result.append(line)
        row = parse_row(line)
        if row and index == last_index[row[0]]:
            program = row[0]
            for bank in sorted(VL_BANKS):
                name = added.get((program, bank))
                if name:
                    result.append(f"-1,{program},{bank},{name}")
    result.extend(mu_lines[end:])
    result[mu_lines.index("Name=YAMAHA MU1000/MU2000")] = "Name=Mu2026 Hybrid"
    result.insert(2, "; MU2000 with VL/PVL/SG banks from S-YXG2026 Hybrid")
    return result


def reaper_lines(qws_lines):
    banks = {}
    for line in qws_lines:
        row = parse_row(line)
        if not row:
            continue
        program, bank, name = row
        programs = banks.setdefault(bank, {})
        if program in programs and programs[program] != name:
            raise ValueError(f"Conflicting voices for bank {bank}, program {program}")
        programs[program] = name

    result = [
        "// Mu2026 Hybrid instrument definition for REAPER",
        "// Generated from the combined QWS definition. Programs are zero-based.",
        "",
    ]
    for bank, programs in sorted(banks.items()):
        msb, lsb = divmod(bank, 128)
        label = BANK_NAMES.get(bank, f"MU2000 bank {msb}:{lsb}")
        result.append(f"Bank {msb} {lsb} Mu2026 Hybrid - {label}")
        result.extend(f"{program} {name}" for program, name in sorted(programs.items()))
        result.append("")
    result.pop()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mu_qws", type=Path)
    parser.add_argument("hybrid_qws", type=Path)
    parser.add_argument("output_qws", type=Path)
    parser.add_argument("output_reaper", type=Path)
    args = parser.parse_args()

    combined = combine(read_lines(args.mu_qws), read_lines(args.hybrid_qws))
    reaper = reaper_lines(combined)
    for path, lines in ((args.output_qws, combined),
                        (args.output_reaper, reaper)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))
    print(f"Wrote {args.output_qws} and {args.output_reaper}")


if __name__ == "__main__":
    main()
