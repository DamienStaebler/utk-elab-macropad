import time
import sys
import serial
import serial.tools.list_ports
from pynput.keyboard import Controller, Key


class Event:
    """Event context passed directly to handlers with state, timing, and keyboard control."""
    def __init__(self, name, action, raw_cmd, daemon, duration=0.0):
        self.name = name           # e.g., "button_1", "encoder"
        self.action = action.lower()  # "press", "release", "+1", "-1"
        self.raw_cmd = raw_cmd
        self.duration = duration   # Hold duration in seconds (populated on release)
        self.keyboard = daemon.keyboard
        self._daemon = daemon

    @property
    def is_press(self) -> bool:
        return self.action == "press"

    @property
    def is_release(self) -> bool:
        return self.action == "release"

    def switch_profile(self, profile_name: str):
        """Switches the active MacroPad profile directly from the event object."""
        self._daemon.set_profile(profile_name)

    def press_combo(self, *keys):
        key_map = {
            Key.ctrl: Key.ctrl_l,
            Key.alt: Key.alt_l,
            Key.shift: Key.shift_l,
            Key.cmd: Key.cmd_l,
        }
        
        normalized = [key_map.get(k, k) for k in keys]
        modifiers = normalized[:-1]
        final_key = normalized[-1]

        for mod in modifiers:
            self.keyboard.press(mod)

        self.keyboard.press(final_key)
        self.keyboard.release(final_key)

        for mod in reversed(modifiers):
            self.keyboard.release(mod)

    def __repr__(self):
        dur_str = f", duration={self.duration:.2f}s" if self.is_release else ""
        return f"<Event name='{self.name}', action='{self.action}'{dur_str}>"


class MacroPad:
    def __init__(self, baud_rate=115200, default_profile="default", cols=4):
        self.baud_rate = baud_rate
        self.keyboard = Controller()
        self.current_profile = default_profile
        self.cols = cols
        self.handlers = {}  # Mapping: (profile_name, event_key) -> function
        self._press_timestamps = {}  # Tracks press start times: (profile, event_key) -> float

    def set_profile(self, profile_name: str):
        """Switch active profile at runtime."""
        print(f"[HOST] Switched profile: '{self.current_profile}' -> '{profile_name}'")
        self.current_profile = profile_name

    def register(self, event_key: str, profile: str):
        """Register a handler strictly to a specific profile and event key."""
        def decorator(func):
            self.handlers[(profile, event_key)] = func
            return func
        return decorator

    def on_profile(self, profile_name: str):
        """Creates a profile-scoped decorator builder."""
        class ProfileBinder:
            def __init__(self, daemon, prof):
                self.daemon = daemon
                self.prof = prof

            def button(self, row: int, col: int):
                """Binds to any event on grid button at (row, col). Triggers on press and release."""
                num = (row - 1) * 4 + col
                return self.daemon.register(f"BTN:{num}", profile=self.prof)

            def button_id(self, num: int):
                """Binds directly by button index ID (1-indexed)."""
                return self.daemon.register(f"BTN:{num}", profile=self.prof)

            def encoder(self, direction):
                """Binds to encoder steps ('+1', '-1', etc.)."""
                formatted_dir = str(direction)
                if not formatted_dir.startswith(("+", "-")):
                    formatted_dir = f"+{formatted_dir}"
                return self.daemon.register(f"ENC:{formatted_dir}", profile=self.prof)

        return ProfileBinder(self, profile_name)

    def auto_detect_port(self):
        """Locates the ESP32 USB CDC / Serial port automatically."""
        ports = serial.tools.list_ports.comports()
        for port_info in ports:
            device_name = port_info.device
            description = port_info.description.lower()
            if "acm" in device_name.lower() or "usb" in device_name.lower() or "espressif" in description:
                return device_name
        return ports[0].device if ports else None

    def handle_serial_command(self, command_str: str):
        command_str = command_str.strip()
        if not command_str:
            return

        parts = command_str.split(":")
        action = parts[1] if len(parts) > 1 else "trigger"
        duration = 0.0

        # Determine lookup key and construct Event payload
        if parts[0] == "BTN" and len(parts) == 3:
            event_key = f"BTN:{parts[2]}"
            name = f"button_{parts[2]}"
            state_key = (self.current_profile, event_key)

            # Calculate press duration on release
            if action.upper() == "PRESS":
                self._press_timestamps[state_key] = time.time()
            elif action.upper() == "RELEASE":
                start_time = self._press_timestamps.pop(state_key, None)
                if start_time:
                    duration = time.time() - start_time

        elif parts[0] == "ENC" and len(parts) == 2:
            event_key = command_str
            name = "encoder"
        else:
            event_key = command_str
            name = "custom"

        event = Event(
            name=name,
            action=action,
            raw_cmd=command_str,
            daemon=self,
            duration=duration
        )

        # Strict lookup: MUST match active profile
        handler = self.handlers.get((self.current_profile, event_key))

        if handler:
            handler(event)
        else:
            print(f"[HOST] Unhandled command '{command_str}' in profile '{self.current_profile}'")

    def run(self):
        print(f"=== ESP32 Serial Daemon Running (Active Profile: '{self.current_profile}') ===")
        while True:
            target_port = self.auto_detect_port()
            if not target_port:
                print("[SEARCHING] No serial device found. Retrying in 2s...")
                time.sleep(2)
                continue

            print(f"[CONNECTED] Connected on port: {target_port}")
            try:
                with serial.Serial(target_port, self.baud_rate, timeout=1) as ser:
                    print(f"[READY] Listening for ESP32 events...")
                    while True:
                        line = ser.readline().decode("utf-8", errors="ignore")
                        if line:
                            self.handle_serial_command(line)
            except (serial.SerialException, OSError) as err_msg:
                print(f"[DISCONNECTED] Lost connection ({err_msg}). Reconnecting in 2s...")
                time.sleep(2)