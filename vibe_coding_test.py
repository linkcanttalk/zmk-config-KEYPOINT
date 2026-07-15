#!/usr/bin/env python3
"""Vibe Coding BLE Test Client

Usage:
    python vibe_coding_test.py scan          # Scan for BLE devices
    python vibe_coding_test.py idle          # Send IDLE state (0)
    python vibe_coding_test.py running       # Send RUNNING state (1)
    python vibe_coding_test.py warning       # Send WARNING state (2)
    python vibe_coding_test.py critical      # Send CRITICAL state (3)
    python vibe_coding_test.py watch         # Watch mode: send states based on input
"""

import asyncio
import sys
from bleak import BleakClient, BleakScanner

SVC_UUID = "00001234-0000-1000-8000-00805f9b34fb"
CHAR_UUID = "00005678-0000-1000-8000-00805f9b34fb"

STATES = {
    "idle": 0,
    "running": 1,
    "warning": 2,
    "critical": 3,
}


async def scan():
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        name = d.name or "Unknown"
        print(f"  {d.address}  {name}")
    print(f"\nFound {len(devices)} devices.")


async def send_state(address: str, state: int):
    async with BleakClient(address) as client:
        print(f"Connected to {address}")
        await client.write_gatt_char(CHAR_UUID, bytes([state]), response=True)
        state_name = [k for k, v in STATES.items() if v == state][0]
        print(f"Sent state: {state_name} ({state})")


async def watch(address: str):
    print(f"Connecting to {address}...")
    async with BleakClient(address) as client:
        print(f"Connected. Type state name to send (idle/running/warning/critical).")
        print("Type 'quit' to exit.\n")
        while True:
            cmd = await asyncio.get_event_loop().run_in_executor(None, input, "> ")
            cmd = cmd.strip().lower()
            if cmd == "quit":
                break
            if cmd in STATES:
                await client.write_gatt_char(CHAR_UUID, bytes([STATES[cmd]]), response=True)
                print(f"Sent: {cmd} ({STATES[cmd]})")
            else:
                print(f"Unknown state: {cmd}")


async def find_address():
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        if d.name and "KEYPOINT" in d.name.upper():
            return d.address
    return None


async def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    cmd = sys.argv[1].lower()

    if cmd == "scan":
        await scan()
        return

    if cmd not in STATES:
        print(f"Unknown command: {cmd}")
        print(__doc__)
        return

    address = sys.argv[2] if len(sys.argv) > 2 else await find_address()
    if not address:
        print("Keyboard not found. Run 'scan' to see available devices.")
        return

    if cmd == "watch":
        await watch(address)
    else:
        await send_state(address, STATES[cmd])


if __name__ == "__main__":
    asyncio.run(main())
