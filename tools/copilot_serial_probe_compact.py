import argparse
import sys
import time

try:
    import serial
except Exception as exc:
    print(f"IMPORT_ERROR: {exc}", file=sys.stderr)
    sys.exit(2)


def drain_available(port, emit):
    chunks = []
    while True:
        waiting = port.in_waiting
        if waiting <= 0:
            break
        data = port.read(waiting)
        if not data:
            break
        chunks.append(data)
        time.sleep(0.02)
    if chunks and emit:
        text = b"".join(chunks).decode("utf-8", errors="replace")
        sys.stdout.write(text)
        sys.stdout.flush()


def read_for(port, seconds, emit):
    deadline = time.time() + seconds
    while time.time() < deadline:
        drain_available(port, emit)
        time.sleep(0.05)
    drain_available(port, emit)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("--pre-read", type=float, default=8.0)
    parser.add_argument("--post-read", type=float, default=2.0)
    parser.add_argument("--monitor", type=float, default=0.0)
    parser.add_argument("--command", action="append", default=[])
    parser.add_argument("--command-file")
    parser.add_argument("commands", nargs="*")
    args = parser.parse_args()

    commands = list(args.command)
    if args.command_file:
        with open(args.command_file, "r", encoding="utf-8") as file:
            commands.extend(line.strip() for line in file if line.strip())
    commands.extend(args.commands)

    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        port.dtr = False
        port.rts = False
        time.sleep(args.settle)

        read_for(port, args.pre_read, emit=False)

        for command in commands:
            print(f"\n>>> {command}")
            port.write((command + "\n").encode("utf-8"))
            port.flush()
            read_for(port, args.post_read, emit=True)

        if args.monitor > 0:
            print(f"\n>>> monitor {args.monitor:.1f}s")
            read_for(port, args.monitor, emit=True)


if __name__ == "__main__":
    main()
