#!/usr/bin/env python3
"""Restart the board and print its serial output for a few seconds, then exit.

Made for AI tools, which can't use an interactive monitor:
    python3 tools/serial_log.py            15 seconds, port found automatically
    python3 tools/serial_log.py 30         30 seconds
    python3 tools/serial_log.py 30 PORT    a specific port, for example /dev/cu.usbserial-110 or COM5
    python3 tools/serial_log.py 30 PORT --no-reset

Needs pyserial: pip install pyserial  (PlatformIO's own Python already has it:
~/.platformio/penv/bin/python on macOS and Linux, %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe on Windows)
"""
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit('pyserial is missing: pip install pyserial')

CH340 = 0x1A86   # USB serial chip on the Cheap Yellow Display


def find_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if p.vid == CH340:
            return p.device
    for p in ports:
        if any(k in p.device for k in ('usbserial', 'wchusbserial', 'ttyUSB', 'SLAB')):
            return p.device
    names = ', '.join(p.device for p in ports) or 'none'
    sys.exit(f'No board found. Serial ports: {names}. Is the cable a data cable?')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    seconds = float(args[0]) if args else 15.0
    port = args[1] if len(args) > 1 else find_port()
    reset = '--no-reset' not in sys.argv

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False          # keep IO0 high, so the board starts the program and not the bootloader
    s.rts = False
    s.open()
    if reset:              # pulse EN through RTS: a clean restart, so you see the boot messages
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    print(f'--- {port}, {seconds:.0f} s ---', flush=True)
    end = time.time() + seconds
    try:
        while time.time() < end:
            line = s.readline()
            if line:
                sys.stdout.write(line.decode('utf-8', 'replace'))
                sys.stdout.flush()
    finally:
        s.close()
    print('--- end ---')


if __name__ == '__main__':
    main()
