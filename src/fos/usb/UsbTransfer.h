#pragma once

// Book transfer over the USB cable. The Sticky's USB-C port reaches the ESP32's
// UART0 through a CH343 serial bridge, so the card cannot appear as a drive;
// tools/usb-books/usb_books.py talks to this receiver instead.
//
// Line protocol (the PC sends "@fos <command>\n"; replies are "@fos ...\n" and
// share the line with the log, so the PC ignores every other line):
//   hello                     -> ok FOs-usb 1
//   baud <rate>               -> ok, then both ends switch (back to 115200 on "end"
//                                or after 30 s without a command)
//   ls [-a] <dir>             -> f <size> <name> / d 0 <name> lines, then end
//                                (-a also lists hidden entries)
//   mkdir <dir>               -> ok | err <reason>
//   rm <file>                 -> ok | err <reason>   (files only)
//   put <size> <crc32> <path> -> ready, then the PC sends raw bytes in 4096-byte
//   put! ...                     chunks and waits for "ack <total>" after each;
//                                done | err <reason>. "put" refuses an existing
//                                file, "put!" replaces it. The data goes to
//                                <path>.part and replaces <path> only when the
//                                CRC-32 matches, both as received and as read
//                                back from the card. The PC may send the next
//                                chunk before the previous ack (2 in flight).
//   crc <file>                -> ok <size> <crc32> (read from the card) | err
//   end                       -> ok

namespace fos::usb {

// Call once in setup().
void begin();

// Call every loop. Serves a command when one arrives; a transfer blocks until it
// finishes. Returns true when it handled anything (counts as user activity).
bool poll();

}  // namespace fos::usb
