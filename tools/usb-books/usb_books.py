#!/usr/bin/env python3
"""Copy books to the FOs Sticky over the USB cable.

The Sticky's USB-C port is a serial link (CH343 bridge), not a drive, so this
script talks to the receiver in the firmware (src/fos/usb/UsbTransfer.cpp).
Wake the device first (press the button); it must be awake, not on the sleep
screen. Needs pyserial: `pip install pyserial`.

    python tools/usb-books/usb_books.py book.epub other.epub   # copy to /books
    python tools/usb-books/usb_books.py --list                 # what is in /books
    python tools/usb-books/usb_books.py --delete "Old book.epub"
    python tools/usb-books/usb_books.py book.epub --replace    # overwrite without asking

Options: --port COM9 (default: found by USB id), --to /folder (default /books).
Opening the port never resets the device: DTR and RTS stay released.
"""

import argparse
import os
import sys
import time
import zlib

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("This tool needs pyserial: pip install pyserial")

BASE_BAUD = 115200
FAST_BAUD = 921600
CHUNK = 4096
CH343_IDS = {(0x1A86, 0x55D3), (0x1A86, 0x55D4), (0x1A86, 0x7523)}


class DeviceError(Exception):
    pass


class Sticky:
    def __init__(self, port):
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = BASE_BAUD
        self.s.timeout = 0.2
        self.s.dtr = False  # DTR/RTS drive the chip's boot and reset lines
        self.s.rts = False
        self.s.open()
        self.buf = b""

    def close(self):
        self.s.close()

    def send(self, command):
        self.s.write(("@fos %s\n" % command).encode("utf-8"))

    def reply(self, timeout=3.0):
        """Next '@fos ...' line; the log shares the link, other lines are skipped."""
        end = time.time() + timeout
        while time.time() < end:
            nl = self.buf.find(b"\n")
            if nl < 0:
                self.buf += self.s.read(256)
                continue
            raw, self.buf = self.buf[:nl], self.buf[nl + 1:]
            at = raw.find(b"@fos ")
            if at >= 0:
                return raw[at + 5:].decode("utf-8", "replace").strip()
        return None

    def command(self, command, timeout=3.0):
        self.send(command)
        answer = self.reply(timeout)
        if answer is None:
            raise DeviceError("no answer to '%s'" % command.split(" ")[0])
        if answer.startswith("err"):
            raise DeviceError(answer[4:] or "error")
        return answer

    def connect(self):
        self.s.reset_input_buffer()
        for _ in range(3):
            self.send("hello")
            answer = self.reply(1.5)
            if answer and answer.startswith("ok FOs-usb"):
                break
        else:
            raise DeviceError("the device does not answer. Wake it (press the button) and try again; "
                              "it does not listen while on the sleep screen.")
        # Faster link for the transfer; the device drops back on 'end' or after 30 s idle.
        self.command("baud %d" % FAST_BAUD)
        time.sleep(0.05)
        self.s.baudrate = FAST_BAUD
        self.s.reset_input_buffer()
        self.buf = b""
        self.send("hello")
        if not (self.reply(2.0) or "").startswith("ok FOs-usb"):
            raise DeviceError("the faster link did not come up; wait 30 s and try again")

    def finish(self):
        try:
            self.command("end")
        except DeviceError:
            pass

    def listing(self, folder):
        self.send("ls %s" % folder)
        entries = []
        while True:
            answer = self.reply(5.0)
            if answer is None:
                raise DeviceError("listing timed out")
            if answer == "end":
                return entries
            if answer.startswith("err"):
                raise DeviceError(answer[4:])
            kind, size, name = answer.split(" ", 2)
            entries.append((kind, int(size), name))

    def put(self, data, path, replace, progress):
        crc = zlib.crc32(data) & 0xFFFFFFFF
        self.command("%s %d %08x %s" % ("put!" if replace else "put", len(data), crc, path), timeout=10)
        # Two chunks in flight (the device buffers 16 KB): the next chunk is on the
        # wire while the device writes the previous one to the card.
        sent = acked = 0
        while acked < len(data):
            while sent < len(data) and sent - acked < 2 * CHUNK:
                piece = data[sent:sent + CHUNK]
                self.s.write(piece)
                sent += len(piece)
            answer = self.reply(10.0)
            if answer is None:
                raise DeviceError("the device stopped answering")
            if answer.startswith("err"):
                raise DeviceError(answer[4:])
            if answer.startswith("ack "):
                acked = int(answer[4:])
                progress(acked, len(data))
        answer = self.reply(60.0)  # the device reads the file back to check it
        if answer != "done":
            raise DeviceError((answer or "no answer")[4:] if answer and answer.startswith("err") else
                              "transfer not confirmed")


def find_port():
    ports = list(list_ports.comports())
    for p in ports:
        if (p.vid, p.pid) in CH343_IDS:
            return p.device
    names = ", ".join(p.device for p in ports) or "none"
    sys.exit("Sticky not found on USB (ports: %s). Plug it in, or pass --port." % names)


def card_name(local_path):
    name = os.path.basename(local_path)
    while ".." in name:  # the device refuses ".." in paths
        name = name.replace("..", ".")
    return name.lstrip(".") or "book"


def bar(done, total):
    width = 30
    filled = int(width * done / total) if total else width
    sys.stdout.write("\r  [%s%s] %3d%%" % ("#" * filled, "." * (width - filled), 100 * done // max(total, 1)))
    sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", help="books to copy")
    parser.add_argument("--port", help="serial port, e.g. COM9 (default: found by USB id)")
    parser.add_argument("--to", default="/books", help="folder on the card (default /books)")
    parser.add_argument("--replace", action="store_true", help="overwrite books already on the card")
    parser.add_argument("--list", action="store_true", help="list the folder on the card")
    parser.add_argument("--delete", metavar="NAME", action="append", default=[], help="delete a file in the folder")
    args = parser.parse_args()
    if not (args.files or args.list or args.delete):
        parser.print_help()
        return 1
    folder = "/" + args.to.strip("/") if args.to.strip("/") else "/"
    prefix = folder.rstrip("/") + "/"
    for f in args.files:
        if not os.path.isfile(f):
            sys.exit("Not a file: %s" % f)

    dev = Sticky(args.port or find_port())
    failed = 0
    try:
        dev.connect()
        if folder != "/":
            dev.command("mkdir %s" % folder)
        existing = {name for kind, _, name in dev.listing(folder) if kind == "f"}
        for name in args.delete:
            try:
                dev.command("rm %s%s" % (prefix, name))
                existing.discard(name)
                print("Deleted %s" % name)
            except DeviceError as e:
                failed += 1
                print("Could not delete %s: %s" % (name, e))
        for local in args.files:
            name = card_name(local)
            replace = args.replace
            if name in existing and not replace:
                if not sys.stdin.isatty():
                    print("Skipped %s: already on the card (use --replace)" % name)
                    continue
                if input("%s is already on the card. Replace it? [y/N] " % name).strip().lower() not in ("y", "yes"):
                    print("Skipped %s" % name)
                    continue
                replace = True
            with open(local, "rb") as fh:
                data = fh.read()
            print("Copying %s (%.1f MB)" % (name, len(data) / 1e6))
            start = time.time()
            try:
                dev.put(data, prefix + name, replace, bar)
                existing.add(name)
                print("\r  done in %.0f s%s" % (time.time() - start, " " * 30))
            except DeviceError as e:
                failed += 1
                print("\n  failed: %s" % e)
                time.sleep(0.5)
                dev.s.reset_input_buffer()
                dev.buf = b""
        if args.list:
            entries = dev.listing(folder)
            print("%s (%d items)" % (folder, len(entries)))
            for kind, size, name in sorted(entries, key=lambda e: e[2].lower()):
                print("  %s  %s" % ("     <dir>" if kind == "d" else "%8.1f KB" % (size / 1024), name))
    except DeviceError as e:
        print("Error: %s" % e)
        return 1
    finally:
        dev.finish()
        dev.close()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
