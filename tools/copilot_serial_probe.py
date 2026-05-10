import argparse
import sys
import time

try:
    import serial
except Exception as exc:
    print(f"IMPORT_ERROR: {exc}", file=sys.stderr)
    sys.exit(2)


def read_available(port):
    chunks = []
    while True:
        waiting = port.in_waiting
        if waiting <= 0:
            break
        data = port.read(waiting)
        if not data:
            break
        chunks.append(data)
        time.sleep(0.05)
    if chunks:
        text = b"".join(chunks).decode("utf-8", errors="replace")
        sys.stdout.write(text)
        sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--pre-read", type=float, default=3.0)
    parser.add_argument("--post-read", type=float, default=2.0)
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("commands", nargs="*")
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        port.dtr = False
        port.rts = False
        time.sleep(args.settle)

        deadline = time.time() + args.pre_read
        while time.time() < deadline:
            read_available(port)
            time.sleep(0.1)

        for command in args.commands:
            print(f"\n>>> {command}")
            port.write((command + "\n").encode("utf-8"))
            port.flush()
            deadline = time.time() + args.post_read
            while time.time() < deadline:
                read_available(port)
                time.sleep(0.1)

        read_available(port)


if __name__ == "__main__":
    main()
