#!/usr/bin/env python3
"""Vibe Coding BLE GUI Debugger

A simple GUI for testing the Vibe Coding BLE GATT service.
Requires: pip install bleak
"""

import asyncio
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext
from bleak import BleakClient, BleakScanner

SVC_UUID = "00001234-0000-1000-8000-00805f9b34fb"
CHAR_UUID = "00005678-0000-1000-8000-00805f9b34fb"

STATES = {
    "idle": 0,
    "running": 1,
    "warning": 2,
    "critical": 3,
}


class VibeCodingGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Vibe Coding BLE Debugger")
        self.root.geometry("480x580")
        self.root.resizable(False, False)

        self.client = None
        self.connected = False
        self.loop = None
        self.thread = None

        self._build_ui()
        self._start_loop()

    def _build_ui(self):
        # Device selection
        frame_device = ttk.LabelFrame(self.root, text="Device", padding=8)
        frame_device.pack(fill="x", padx=10, pady=(10, 5))

        self.device_var = tk.StringVar()
        self.device_combo = ttk.Combobox(frame_device, textvariable=self.device_var, state="readonly", width=42)
        self.device_combo.pack(side="left", padx=(0, 5))

        self.scan_btn = ttk.Button(frame_device, text="Scan", command=self._scan)
        self.scan_btn.pack(side="left")

        # Connect / Disconnect
        frame_conn = ttk.LabelFrame(self.root, text="Connection", padding=8)
        frame_conn.pack(fill="x", padx=10, pady=5)

        self.connect_btn = ttk.Button(frame_conn, text="Connect", command=self._connect)
        self.connect_btn.pack(side="left", padx=(0, 5))

        self.disconnect_btn = ttk.Button(frame_conn, text="Disconnect", command=self._disconnect, state="disabled")
        self.disconnect_btn.pack(side="left")

        self.status_label = ttk.Label(frame_conn, text="Disconnected", foreground="gray")
        self.status_label.pack(side="right")

        # State buttons
        frame_state = ttk.LabelFrame(self.root, text="Vibe Coding State", padding=8)
        frame_state.pack(fill="x", padx=10, pady=5)

        for i, (name, value) in enumerate(STATES.items()):
            btn = tk.Button(
                frame_state,
                text=f"{name.upper()}\n({value})",
                font=("Helvetica", 10, "bold"),
                width=8,
                height=2,
                command=lambda n=name: self._send_state(n),
            )
            btn.grid(row=0, column=i, padx=4)

        # Current state indicator
        frame_indicator = ttk.LabelFrame(self.root, text="Current State", padding=8)
        frame_indicator.pack(fill="x", padx=10, pady=5)

        self.state_indicator = tk.Label(frame_indicator, text="IDLE",
                                        font=("Helvetica", 16, "bold"), width=12)
        self.state_indicator.pack()

        # Log
        frame_log = ttk.LabelFrame(self.root, text="Log", padding=8)
        frame_log.pack(fill="both", expand=True, padx=10, pady=(5, 10))

        self.log = scrolledtext.ScrolledText(frame_log, height=12, state="disabled", font=("Courier", 9))
        self.log.pack(fill="both", expand=True)

    def _log(self, msg):
        self.log.configure(state="normal")
        self.log.insert("end", msg + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _start_loop(self):
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.loop.run_forever, daemon=True)
        self.thread.start()

    def _run(self, coro):
        future = asyncio.run_coroutine_threadsafe(coro, self.loop)
        return future.result(timeout=10)

    def _scan(self):
        self.scan_btn.configure(state="disabled")
        self._log("Scanning for BLE devices...")

        def do():
            try:
                devices = self._run(BleakScanner.discover(timeout=5.0))
                self.root.after(0, self._on_scan_done, devices)
            except Exception as e:
                self.root.after(0, self._log, f"Scan error: {e}")
                self.root.after(0, lambda: self.scan_btn.configure(state="normal"))

        threading.Thread(target=do, daemon=True).start()

    def _on_scan_done(self, devices):
        self.scan_btn.configure(state="normal")
        entries = []
        for d in devices:
            name = d.name or "Unknown"
            if "zitaotech" in name.lower():
                entries.append(f"{d.address}  {name}")
        self.device_combo["values"] = entries
        if entries:
            self.device_combo.current(0)
            self._log(f"Found {len(entries)} ZitaTech device(s).")
        else:
            self._log("No ZitaTech devices found.")

    def _connect(self):
        sel = self.device_combo.get()
        if not sel:
            self._log("No device selected.")
            return
        address = sel.split("  ")[0]
        self._do_connect(address)

    def _do_connect(self, address):
        self._log(f"Connecting to {address}...")
        self.connect_btn.configure(state="disabled")

        def do():
            try:
                self.client = BleakClient(address)
                self._run(self.client.connect())
                self.connected = True
                self.root.after(0, self._on_connected)
            except Exception as e:
                self.root.after(0, self._log, f"Connect error: {e}")
                self.root.after(0, lambda: self.connect_btn.configure(state="normal"))

        threading.Thread(target=do, daemon=True).start()

    def _on_connected(self):
        self._log(f"Connected to {self.client.address}")
        self.status_label.configure(text="Connected", foreground="green")
        self.connect_btn.configure(state="disabled")
        self.disconnect_btn.configure(state="normal")

    def _disconnect(self):
        if not self.connected:
            return
        self._log("Disconnecting...")

        def do():
            try:
                self._run(self.client.disconnect())
            except Exception:
                pass
            self.connected = False
            self.client = None
            self.root.after(0, self._on_disconnected)

        threading.Thread(target=do, daemon=True).start()

    def _on_disconnected(self):
        self._log("Disconnected.")
        self.status_label.configure(text="Disconnected", foreground="gray")
        self.connect_btn.configure(state="normal")
        self.disconnect_btn.configure(state="disabled")

    def _send_state(self, name):
        if not self.connected:
            self._log("Not connected.")
            return
        value = STATES[name]

        def do():
            try:
                self._run(self.client.write_gatt_char(CHAR_UUID, bytes([value]), response=True))
                self.root.after(0, self._log, f"Sent: {name.upper()} ({value})")
                self.root.after(0, self._update_indicator, name)
            except Exception as e:
                self.root.after(0, self._log, f"Send error: {e}")

        threading.Thread(target=do, daemon=True).start()

    def _update_indicator(self, name):
        self.state_indicator.configure(text=name.upper())


def main():
    root = tk.Tk()
    VibeCodingGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
