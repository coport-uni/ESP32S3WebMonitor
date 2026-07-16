# Captures the BOX-3 boot log for a fixed number of seconds and exits, so a
# non-interactive session can read it. `idf.py monitor` is interactive and
# would hang forever waiting on Ctrl+]. Lifetime: delete once bring-up is done.
# Usage: python read_boot_log.py [seconds] [--reset]
import sys
import time

import serial

PORT = "COM15"
BAUD = 115200
SECS = float(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else 20.0
RESET = "--reset" in sys.argv

s = serial.Serial(PORT, BAUD, timeout=0.2)

if RESET:
    # The USB-Serial-JTAG bridge maps DTR/RTS to EN/BOOT the same way esptool
    # drives them; toggling them reboots the chip so the log starts at boot.
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)
    time.sleep(0.1)

deadline = time.time() + SECS
buf = b""
while time.time() < deadline:
    chunk = s.read(4096)
    if chunk:
        buf += chunk
        sys.stdout.write(chunk.decode("utf-8", "replace"))
        sys.stdout.flush()
s.close()
sys.stderr.write("\n--- captured %d bytes in %.0fs ---\n" % (len(buf), SECS))
