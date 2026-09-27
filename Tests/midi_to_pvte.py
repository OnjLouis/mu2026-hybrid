"""Convert a bounded Standard MIDI File prefix to HybridHostProbe events."""

import argparse
from pathlib import Path

import mido


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("midi", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--limit", type=int, default=150)
    parser.add_argument("--channel", type=int, choices=range(16))
    args = parser.parse_args()

    rows = ["PVTE 1"]
    for event in mido.merge_tracks(mido.MidiFile(args.midi).tracks):
        if len(rows) > args.limit:
            break
        if event.is_meta:
            continue
        if args.channel is not None and hasattr(event, "channel") \
                and event.channel != args.channel:
            continue
        if event.type == "sysex":
            rows.append("S " + bytes(event.bytes()).hex().upper())
        elif event.type in {
            "note_on", "note_off", "control_change", "program_change",
            "pitchwheel", "polytouch", "aftertouch",
        }:
            rows.append("M " + bytes(event.bytes()).hex().upper())
    args.output.write_text("\n".join(rows) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
