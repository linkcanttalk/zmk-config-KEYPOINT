#!/usr/bin/env python3
"""
Send Vibe Coding Status to ZMK keyboard via Raw HID (USB or Bluetooth).

Usage:
    python3 send_vibe_status.py <status>          # Send once
    python3 send_vibe_status.py <status> --loop   # Send every 2 seconds

Status values:
    0 - IDLE
    1 - RUNNING
    2 - WARNING
    3 - CRITICAL

Requirements:
    pip install hidapi

Note:
    - For USB: Keyboard must be connected via USB cable
    - For Bluetooth: Keyboard must be paired and connected via BLE
    - On macOS, you may need to run with sudo for Bluetooth HID access
"""

import sys
import time
import signal
import hid

# ZMK Raw HID settings (from Kconfig)
USAGE_PAGE = 0xFF60
USAGE = 0x61
VIBE_CODING_STATUS_TYPE = 0xBB

STATUS_NAMES = ['IDLE', 'RUNNING', 'WARNING', 'CRITICAL']

# Loop interval in seconds
LOOP_INTERVAL = 2

running = True

def signal_handler(sig, frame):
    global running
    print("\nStopping...")
    running = False

def find_keyboards():
    """Find all ZMK keyboard HID devices (USB and Bluetooth)."""
    devices = []
    for device in hid.enumerate():
        product = device.get('product_string', '').upper()
        if 'ZMK' in product or 'CORNE' in product or 'KEYPOINT' in product:
            if device.get('usage_page') == USAGE_PAGE and device.get('usage') == USAGE:
                devices.append(device)
    return devices

def send_status(status, device_path=None):
    """Send vibe coding status to keyboard. Returns True on success."""
    devices = find_keyboards()
    if not devices:
        print("Error: no keyboard found.")
        return False

    if device_path:
        device_info = next((d for d in devices if d['path'] == device_path), None)
        if not device_info:
            print(f"Error: device not found at path {device_path}")
            return False
    elif len(devices) == 1:
        device_info = devices[0]
    else:
        device_info = devices[0]

    try:
        device = hid.device()
        device.open_path(device_info['path'])

        report = [0x00] * 32
        report[0] = 0x00
        report[1] = VIBE_CODING_STATUS_TYPE
        report[2] = status

        device.write(bytes(report))
        time.sleep(0.05)
        device.close()
        return True
    except Exception as e:
        print(f"Error: {e}")
        return False

def list_devices():
    """List all available HID devices."""
    devices = find_keyboards()
    if not devices:
        print("No ZMK keyboards found.")
        return

    print("Found ZMK keyboards:")
    for dev in devices:
        connection = "USB" if dev.get('interface_number', -1) >= 0 else "Bluetooth"
        print(f"  - {dev['product_string']} ({connection})")
        print(f"    Path: {dev['path']}")
        print(f"    VID: 0x{dev['vendor_id']:04x}, PID: 0x{dev['product_id']:04x}")

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] in ['-h', '--help']:
        print(__doc__)
        sys.exit(0)

    if sys.argv[1] == '-l' or sys.argv[1] == '--list':
        list_devices()
        sys.exit(0)

    try:
        status = int(sys.argv[1])
    except ValueError:
        print("Error: status must be a number (0-3)")
        sys.exit(1)

    if status < 0 or status > 3:
        print("Error: status must be 0-3 (idle/running/warning/critical)")
        sys.exit(1)

    loop_mode = '--loop' in sys.argv

    if loop_mode:
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        print(f"Sending {STATUS_NAMES[status]} every {LOOP_INTERVAL}s (Ctrl+C to stop)")
        count = 0
        while running:
            count += 1
            if send_status(status):
                print(f"[{count}] Sent {STATUS_NAMES[status]}")
            else:
                print(f"[{count}] Failed, retrying in {LOOP_INTERVAL}s...")
            time.sleep(LOOP_INTERVAL)
        print("Stopped.")
    else:
        print(f"Sending status: {STATUS_NAMES[status]}")
        if send_status(status):
            print("Sent successfully!")
        else:
            sys.exit(1)
