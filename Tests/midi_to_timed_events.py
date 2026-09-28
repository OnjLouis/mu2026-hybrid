"""Convert an SMF to sample-positioned events for HybridTimedProbe."""

import argparse
from pathlib import Path

import mido


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("midi", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--rate", type=int, default=44100)
    args = parser.parse_args()

    seconds = 0.0
    with args.output.open("w", encoding="ascii", newline="\n") as output:
        output.write("TIMED_MIDI 1\n")
        for message in mido.MidiFile(args.midi):
            seconds += message.time
            if message.is_meta:
                continue
            kind = "S" if message.type == "sysex" else "M"
            output.write(f"{round(seconds * args.rate)} {kind} "
                         f"{bytes(message.bytes()).hex().upper()}\n")
        output.write(f"{round((seconds + 2.0) * args.rate)} E\n")


if __name__ == "__main__":
    main()
