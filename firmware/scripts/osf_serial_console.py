#!/usr/bin/env python3
"""
OpenSmartFocuser Serial Console
===============================

OS-independent GUI command parser / sender for the OpenSmartFocuser firmware.

Frame format (see include/serial_command_index.h / src/serial_command_index.cpp):
    :<token><payload>#
Payload fields are comma-separated, ints/floats/strings auto-typed by the
firmware PayloadParser.

Requires: pyserial  (pip install pyserial)
Runs on Windows, macOS, Linux with the stdlib tkinter GUI.
"""

import queue
import re
import threading
import time
import tkinter as tk
from dataclasses import dataclass, field
from tkinter import messagebox, scrolledtext, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    serial = None
    list_ports = None


# ---------------------------------------------------------------------------
# Command model
# ---------------------------------------------------------------------------

@dataclass
class ArgSpec:
    """Describes one payload argument."""
    name: str
    kind: str = "int"            # 'int' | 'float' | 'str'
    hint: str = ""               # shown in the input field
    source: str = "user"         # 'user' | 'position' (auto-filled from focuser)


@dataclass
class CommandSpec:
    """Describes one serial command frame."""
    name: str                    # human readable label
    token: str                   # e.g. ':GP'
    args: list = field(default_factory=list)
    response: str = ""           # documented response frame
    desc: str = ""               # short description
    category: str = "General"

    def build_frame(self, values):
        """Build a wire frame from a list of string values."""
        payload = ",".join(v.strip() for v in values if v is not None)
        return f"{self.token}{payload}#"


# Command index mirrored from src/serial_command_index.cpp
COMMANDS = [
    # ---- status ----
    CommandSpec("Get Position", ":GP", [], ":GP<steps>#",
                "Get current position in steps.", "Status"),
    CommandSpec("Get Movement State", ":GM", [], ":GM<state>#",
                "Get movement state: IDLE, MOVING or HOMING.", "Status"),
    CommandSpec("Get Speed", ":GS", [], ":GS<speed>#",
                "Get current speed setting (0-4).", "Status"),
    CommandSpec("Get Limits", ":GL", [], ":GL<minSteps>,<maxSteps>#",
                "Get min/max limits in steps.", "Status"),
    CommandSpec("Set Position", ":SP",
                [ArgSpec("position", "int", "steps")], ":ACK#",
                "Override current position (steps).", "Status"),
    CommandSpec("Get Firmware Version", ":VF", [], ":VF<version>#",
                "Get firmware version (not implemented yet).", "Status"),
    CommandSpec("Heartbeat", ":PP", [], ":PP#",
                "Heartbeat / ping.", "Status"),

    # ---- homing ----
    CommandSpec("Home", ":HM", [], ":ACK#",
                "Start the homing sequence.", "Homing"),

    # ---- starmap target ----
    CommandSpec("Get Target", ":TI", [], ":TI<RA>,<DEC>,<name>#",
                "Get DSO target RA, DEC and name.", "Target"),
    CommandSpec("Set Target", ":TG",
                [ArgSpec("ra", "str", "e.g. 12h30m"),
                 ArgSpec("dec", "str", "e.g. +41d16m"),
                 ArgSpec("name", "str", "target name")], ":ACK#",
                "Set DSO target RA, DEC, name.", "Target"),
    CommandSpec("Clear Target", ":TC", [], ":ACK#",
                "Clear the DSO target.", "Target"),

    # ---- presets ----
    CommandSpec("Goto Preset", ":PG",
                [ArgSpec("presetId", "int", "preset id")], ":ACK#",
                "Goto preset by id.", "Presets"),
    CommandSpec("Get Preset", ":PR",
                [ArgSpec("presetId", "int", "preset id")],
                ":PR<presetId>,<name>,<steps>#",
                "Get preset by id.", "Presets"),
    CommandSpec("List Presets", ":PL", [],
                ":PL<presetId>,<name>,<steps># ... :PL!#",
                "List all presets (terminates with :PL!#).", "Presets"),
    CommandSpec("Add Preset", ":PA",
                [ArgSpec("steps", "int", "-1 = current position"),
                 ArgSpec("presetName", "str", "name")],
                ":PA<presetId>,<presetName>#",
                "Add preset. steps=-1 uses current position.", "Presets"),
    CommandSpec("Set Preset", ":PS",
                [ArgSpec("presetId", "int", "preset id"),
                 ArgSpec("steps", "int", "-1 = current position"),
                 ArgSpec("presetName", "str", "name")], ":ACK#",
                "Set preset by id. steps=-1 uses current position.", "Presets"),
    CommandSpec("Remove Preset", ":PC",
                [ArgSpec("presetId", "int", "preset id")], ":ACK#",
                "Remove preset by id.", "Presets"),

    # ---- config ----
    CommandSpec("Get Motor Current", ":CI", [], ":CI<currentMa>#",
                "Get motor current in mA.", "Config"),
    CommandSpec("Get Microsteps", ":CU", [], ":CU<microsteps>#",
                "Get microstep setting.", "Config"),

    # ---- movement ----
    CommandSpec("Move Absolute", ":MA",
                [ArgSpec("positionSteps", "int", "absolute steps")], ":ACK#",
                "Move to absolute position (steps).", "Movement"),
    CommandSpec("Move Relative", ":MR",
                [ArgSpec("relativeSteps", "int", "+/- steps")], ":ACK#",
                "Move relative number of steps.", "Movement"),
    CommandSpec("Halt", ":MH", [], ":ACK#",
                "Stop motion immediately.", "Movement"),
    CommandSpec("Set Speed", ":MS",
                [ArgSpec("speed", "int", "0-4")], ":ACK#",
                "Set movement speed (0-4).", "Movement"),

    # ---- system ----
    CommandSpec("Reboot", ":RB", [], ":ACK#",
                "Reboot (3 s delay before reboot).", "System"),
    CommandSpec("Toggle Motor", ":TM", [], ":TM<state>#",
                "Toggle motor enabled state.", "System"),
    CommandSpec("Disable Motor", ":DM", [], ":ACK#",
                "Disable motor.", "System"),
    CommandSpec("Enable Motor", ":EM", [], ":ACK#",
                "Enable motor.", "System"),

    # ---- addons ----
    CommandSpec("Query Add-ons", ":AQ", [], ":AQ<Type># ... :AQ!#",
                "Query add-ons (terminates with :AQ!#).", "Add-ons"),
    CommandSpec("Set Flat Panel Brightness", ":FP",
                [ArgSpec("brightness", "int", "0-255")], ":ACK#",
                "Set flat panel brightness (0=off, 255=max).", "Add-ons"),
    CommandSpec("Set Shutter Position", ":SV",
                [ArgSpec("position", "int", "0-180")], ":ACK#",
                "Set shutter position (0=closed, 180=open).", "Add-ons"),
]

COMMAND_BY_NAME = {c.name: c for c in COMMANDS}


# ---------------------------------------------------------------------------
# Serial worker
# ---------------------------------------------------------------------------

class SerialWorker:
    """Threaded serial reader/writer. All events go to a queue for the GUI."""

    def __init__(self, out_queue):
        self._out = out_queue
        self._ser = None
        self._thread = None
        self._stop = threading.Event()
        self._lock = threading.Lock()

    @staticmethod
    def list_ports():
        if list_ports is None:
            return []
        return sorted(p.device for p in list_ports.comports())

    @property
    def is_open(self):
        return self._ser is not None and self._ser.is_open

    def open(self, port, baud=115200, timeout=0.1):
        if serial is None:
            raise RuntimeError("pyserial is not installed (pip install pyserial)")
        self.close()
        self._ser = serial.Serial(port, baudrate=baud, timeout=timeout)
        self._stop.clear()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def close(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None
        with self._lock:
            if self._ser is not None:
                try:
                    self._ser.close()
                except Exception:
                    pass
                self._ser = None

    def send(self, data: str):
        """Send a raw string; newline terminator added if missing."""
        with self._lock:
            if not self.is_open:
                raise RuntimeError("Serial port is not connected.")
            if not data.endswith("\n"):
                data += "\n"
            self._ser.write(data.encode("ascii", errors="replace"))

    def _reader(self):
        buf = b""
        while not self._stop.is_set():
            try:
                with self._lock:
                    ser = self._ser
                if ser is None or not ser.is_open:
                    break
                chunk = ser.read(256)
                if chunk:
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = line.decode("ascii", errors="replace").rstrip("\r")
                        self._out.put(("rx", text))
            except Exception as exc:  # device unplugged, etc.
                self._out.put(("error", f"Serial read error: {exc}"))
                break
        self._out.put(("closed", None))


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------

class SerialConsoleApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("OpenSmartFocuser Serial Console")
        self.geometry("980x640")
        self.minsize(820, 520)

        self.queue = queue.Queue()
        self.worker = SerialWorker(self.queue)

        self._build_menu()
        self._build_connection_bar()
        self._build_command_panel()
        self._build_monitor()

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(60, self._drain_queue)
        self._refresh_ports()
        self._set_connected_ui(False)

    # ---- layout -----------------------------------------------------------

    def _build_menu(self):
        menubar = tk.Menu(self)
        cmd_menu = tk.Menu(menubar, tearoff=0)
        for cat in ("Status", "Homing", "Target", "Presets", "Config",
                    "Movement", "System", "Add-ons"):
            sub = tk.Menu(cmd_menu, tearoff=0)
            for spec in COMMANDS:
                if spec.category == cat:
                    sub.add_command(
                        label=f"{spec.name}  ({spec.token}...#)",
                        command=lambda s=spec: self._select_command_by_name(s.name))
            cmd_menu.add_cascade(label=cat, menu=sub)
        menubar.add_cascade(label="Commands", menu=cmd_menu)
        self.config(menu=menubar)

    def _build_connection_bar(self):
        bar = ttk.LabelFrame(self, text="Connection")
        bar.pack(fill="x", padx=8, pady=(8, 4))

        ttk.Label(bar, text="Port:").pack(side="left", padx=(8, 2), pady=8)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(bar, textvariable=self.port_var,
                                       width=18, state="readonly")
        self.port_combo.pack(side="left", pady=8)

        ttk.Label(bar, text="Baud:").pack(side="left", padx=(10, 2))
        self.baud_var = tk.StringVar(value="115200")
        self.baud_combo = ttk.Combobox(
            bar, textvariable=self.baud_var, width=9, state="readonly",
            values=("9600", "19200", "38400", "57600", "115200",
                    "230400", "460800", "921600"))
        self.baud_combo.pack(side="left")

        self.refresh_btn = ttk.Button(bar, text="Refresh",
                                      command=self._refresh_ports)
        self.refresh_btn.pack(side="left", padx=(10, 2))

        self.connect_btn = ttk.Button(bar, text="Connect",
                                      command=self._toggle_connection)
        self.connect_btn.pack(side="left", padx=2)

        self.status_var = tk.StringVar(value="Disconnected")
        self.status_lbl = ttk.Label(bar, textvariable=self.status_var,
                                    foreground="gray")
        self.status_lbl.pack(side="left", padx=12)

    def _build_command_panel(self):
        panel = ttk.LabelFrame(self, text="Command Builder")
        panel.pack(fill="x", padx=8, pady=4)

        top = ttk.Frame(panel)
        top.pack(fill="x", padx=8, pady=(8, 2))

        ttk.Label(top, text="Command:").pack(side="left")
        self.cmd_var = tk.StringVar()
        self.cmd_combo = ttk.Combobox(
            top, textvariable=self.cmd_var, state="readonly", width=34,
            values=[f"{c.name}  [{c.category}]" for c in COMMANDS])
        self.cmd_combo.pack(side="left", padx=(4, 8))
        self.cmd_combo.bind("<<ComboboxSelected>>", self._on_command_selected)

        self.desc_var = tk.StringVar(value="Select a command to see its description.")
        ttk.Label(panel, textvariable=self.desc_var,
                  foreground="#444").pack(anchor="w", padx=8)

        self.args_frame = ttk.Frame(panel)
        self.args_frame.pack(fill="x", padx=8, pady=4)
        self.arg_entries = []

        bottom = ttk.Frame(panel)
        bottom.pack(fill="x", padx=8, pady=(2, 8))

        ttk.Label(bottom, text="Frame:").pack(side="left")
        self.frame_var = tk.StringVar()
        self.frame_entry = ttk.Entry(bottom, textvariable=self.frame_var)
        self.frame_entry.pack(side="left", fill="x", expand=True, padx=(4, 8))

        self.send_cmd_btn = ttk.Button(bottom, text="Send Command",
                                       command=self._send_built_command)
        self.send_cmd_btn.pack(side="left")

    def _build_monitor(self):
        mon = ttk.LabelFrame(self, text="Serial Monitor")
        mon.pack(fill="both", expand=True, padx=8, pady=(4, 8))

        self.output = scrolledtext.ScrolledText(
            mon, wrap="word", state="disabled", height=14,
            font=("Consolas", 10))
        self.output.pack(fill="both", expand=True, padx=6, pady=(6, 2))
        self.output.tag_config("tx", foreground="#0057b8")
        self.output.tag_config("rx", foreground="#0a7a0a")
        self.output.tag_config("err", foreground="#c00000")
        self.output.tag_config("sys", foreground="#777777")

        send_row = ttk.Frame(mon)
        send_row.pack(fill="x", padx=6, pady=(2, 6))

        ttk.Label(send_row, text="Send:").pack(side="left")
        self.raw_var = tk.StringVar()
        self.raw_entry = ttk.Entry(send_row, textvariable=self.raw_var)
        self.raw_entry.pack(side="left", fill="x", expand=True, padx=(4, 8))
        self.raw_entry.bind("<Return>", lambda _e: self._send_raw())

        self.send_raw_btn = ttk.Button(send_row, text="Send Raw",
                                       command=self._send_raw)
        self.send_raw_btn.pack(side="left")
        ttk.Button(send_row, text="Clear",
                   command=self._clear_output).pack(side="left", padx=(6, 0))

    # ---- connection ---------------------------------------------------------

    def _refresh_ports(self):
        ports = self.worker.list_ports()
        self.port_combo["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(ports[0])
        elif not ports:
            self.port_var.set("")

    def _toggle_connection(self):
        if self.worker.is_open:
            self.worker.close()
            self._set_connected_ui(False)
            self._log("Disconnected.", "sys")
            return
        port = self.port_var.get()
        if not port:
            messagebox.showwarning("No port", "Select a serial port first.")
            return
        try:
            self.worker.open(port, int(self.baud_var.get()))
        except Exception as exc:
            messagebox.showerror("Connect failed", str(exc))
            return
        self._set_connected_ui(True)
        self._log(f"Connected to {port} @ {self.baud_var.get()} baud.", "sys")

    def _set_connected_ui(self, connected):
        state = "disabled" if connected else "normal"
        for w in (self.port_combo, self.baud_combo, self.refresh_btn):
            w.configure(state="disabled" if connected else
                        ("readonly" if w is not self.refresh_btn else "normal"))
        self.connect_btn.configure(text="Disconnect" if connected else "Connect")
        self.status_var.set("Connected" if connected else "Disconnected")
        self.status_lbl.configure(foreground="#0a7a0a" if connected else "gray")

    # ---- command builder ---------------------------------------------------

    def _select_command_by_name(self, name):
        for item in self.cmd_combo["values"]:
            if item.startswith(name + " "):
                self.cmd_var.set(item)
                break
        self._on_command_selected()

    def _on_command_selected(self, _event=None):
        text = self.cmd_var.get()
        name = text.rsplit("  [", 1)[0]
        spec = COMMAND_BY_NAME.get(name)
        for w in self.args_frame.winfo_children():
            w.destroy()
        self.arg_entries = []
        if spec is None:
            return
        self.desc_var.set(f"{spec.token}...#  →  {spec.response}   {spec.desc}")
        for i, arg in enumerate(spec.args):
            row = ttk.Frame(self.args_frame)
            row.pack(fill="x", pady=1)
            ttk.Label(row, text=f"{arg.name}:", width=16).pack(side="left")
            var = tk.StringVar()
            entry = ttk.Entry(row, textvariable=var)
            entry.pack(side="left", fill="x", expand=True, padx=(4, 4))
            if arg.hint:
                var.set(arg.hint if arg.source != "position" else "")
                entry.configure(foreground="#888888")
                entry.bind("<FocusIn>",
                           lambda e, v=var, en=entry: self._clear_hint(v, en))
            if arg.source == "position":
                ttk.Button(row, text="From focuser", width=12,
                           command=lambda v=var: self._fill_position(v)
                           ).pack(side="left")
            self.arg_entries.append((arg, var))
        self._update_frame_preview()
        for _arg, var in self.arg_entries:
            var.trace_add("write", lambda *_: self._update_frame_preview())

    @staticmethod
    def _clear_hint(var, entry):
        var.set("")
        entry.configure(foreground="black")

    def _fill_position(self, var):
        """Ask the focuser for its current position and use it in the field."""
        self._pending_position_var = var
        try:
            self.worker.send(":GP#")
            self._log("TX: :GP#   (querying position)", "tx")
        except Exception as exc:
            messagebox.showwarning("Not connected", str(exc))

    def _current_spec(self):
        text = self.cmd_var.get()
        if not text:
            return None
        return COMMAND_BY_NAME.get(text.rsplit("  [", 1)[0])

    def _update_frame_preview(self):
        spec = self._current_spec()
        if spec is None:
            return
        values = [var.get() for _arg, var in self.arg_entries]
        self.frame_var.set(spec.build_frame(values))

    def _send_built_command(self):
        spec = self._current_spec()
        if spec is None:
            messagebox.showinfo("No command", "Select a command first.")
            return
        frame = self.frame_var.get().strip() or spec.build_frame(
            [v.get() for _a, v in self.arg_entries])
        self._transmit(frame)

    def _send_raw(self):
        data = self.raw_var.get().strip()
        if not data:
            return
        self._transmit(data)
        self.raw_var.set("")

    def _transmit(self, frame):
        try:
            self.worker.send(frame)
        except Exception as exc:
            messagebox.showwarning("Send failed", str(exc))
            return
        self._log(f"TX: {frame}", "tx")

    # ---- monitor -------------------------------------------------------------

    def _log(self, text, tag="rx"):
        self.output.configure(state="normal")
        self.output.insert("end", text + "\n", tag)
        self.output.see("end")
        self.output.configure(state="disabled")

    def _clear_output(self):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.configure(state="disabled")

    def _drain_queue(self):
        try:
            while True:
                kind, payload = self.queue.get_nowait()
                if kind == "rx":
                    self._log(f"RX: {payload}", "rx")
                    self._maybe_capture_position(payload)
                elif kind == "error":
                    self._log(payload, "err")
                elif kind == "closed":
                    if self.worker.is_open is False and \
                       self.connect_btn.cget("text") == "Disconnect":
                        self._set_connected_ui(False)
                        self._log("Connection closed.", "sys")
        except queue.Empty:
            pass
        self.after(60, self._drain_queue)

    def _maybe_capture_position(self, line):
        """If we asked for :GP and got :GP<steps>#, fill the pending field."""
        var = getattr(self, "_pending_position_var", None)
        if var is None:
            return
        m = re.match(r"^:GP(-?\d+)#", line.strip())
        if m:
            var.set(m.group(1))
            self._pending_position_var = None

    # ---- shutdown -------------------------------------------------------------

    def _on_close(self):
        self.worker.close()
        self.destroy()


def main():
    if serial is None:
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror(
            "Missing dependency",
            "pyserial is required.\nInstall it with:  pip install pyserial")
        return
    app = SerialConsoleApp()
    app.mainloop()


if __name__ == "__main__":
    main()
