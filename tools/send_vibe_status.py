#!/usr/bin/env python3
"""
Send Vibe Coding Status to ZMK keyboard via Raw HID (USB or Bluetooth).

Usage:
    python3 send_vibe_status.py <status>

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
import hid

# ZMK Raw HID settings (from Kconfig)
USAGE_PAGE = 0xFF60
USAGE = 0x61
VIBE_CODING_STATUS_TYPE = 0xBB

STATUS_NAMES = ['IDLE', 'RUNNING', 'WARNING', 'CRITICAL']

def find_keyboards():
    """Find all ZMK keyboard HID devices (USB and Bluetooth)."""
    devices = []
    for device in hid.enumerate(USAGE_PAGE, USAGE):
        devices.append(device)
    # Also try searching by product name if no devices found
    if not devices:
        for device in hid.enumerate():
            if 'ZMK' in device.get('product_string', '').upper() or \
               'CORNE' in device.get('product_string', '').upper() or \
               'KEYPOINT' in device.get('product_string', '').upper():
                if device.get('usage_page') == USAGE_PAGE and device.get('usage') == USAGE:
                    devices.append(device)
    return devices

def send_status(status, device_path=None):
    """Send vibe coding status to keyboard."""
    if status < 0 or status > 3:
        print("Error: status must be 0-3 (idle/running/warning/critical)")
        sys.exit(1)

    devices = find_keyboards()
    if not devices:
        print("Error: no keyboard found.")
        print("Make sure keyboard is connected via USB or paired via Bluetooth.")
        sys.exit(1)

    # Select device
    if device_path:
        device_info = next((d for d in devices if d['path'] == device_path), None)
        if not device_info:
            print(f"Error: device not found at path {device_path}")
            sys.exit(1)
    elif len(devices) == 1:
        device_info = devices[0]
    else:
        print("Multiple keyboards found:")
        for i, dev in enumerate(devices):
            connection = "USB" if dev['usage_page'] == USAGE_PAGE else "BT"
            print(f"  [{i}] {dev['product_string']} ({connection})")
        print("Using first device...")
        device_info = devices[0]

    connection_type = "USB" if device_info.get('interface_number', -1) >= 0 else "Bluetooth"
    print(f"Found keyboard: {device_info['product_string']} ({connection_type})")
    print(f"Sending status: {STATUS_NAMES[status]}")

    try:
        device = hid.Device(path=device_info['path'])

        # Build HID report (32 bytes)
        report = [0x00] * 32
        report[0] = 0x00  # Report ID
        report[1] = VIBE_CODING_STATUS_TYPE  # Data type
        report[2] = status  # Status value

        device.write(bytes(report))
        print("Sent successfully!")

        device.close()
    except PermissionError:
        print("Error: permission denied. Try running with sudo (macOS) or check udev rules (Linux).")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

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

    send_status(status)
