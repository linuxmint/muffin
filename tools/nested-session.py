#!/usr/bin/env python3

import os
import signal
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Gdk', '3.0')
from gi.repository import Gtk, Gdk, GLib

PRESETS = [
    "Full workarea",
    "1920x1080",
    "1280x720",
    "2560x1440",
    "Custom",
]

DEFAULT_COMMAND = "cinnamon --nested --wayland"
TITLEBAR_HEIGHT = 32


class NestedSessionLauncher(Gtk.Window):
    def __init__(self):
        super().__init__(title="Nested Cinnamon Session")
        self.set_default_size(380, -1)
        self.set_resizable(False)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        box.set_margin_top(16)
        box.set_margin_bottom(16)
        box.set_margin_start(16)
        box.set_margin_end(16)
        self.add(box)

        # Resolution selector
        res_label = Gtk.Label(label="Resolution", xalign=0)
        box.pack_start(res_label, False, False, 0)

        self.resolution_combo = Gtk.ComboBoxText()
        for preset in PRESETS:
            self.resolution_combo.append_text(preset)
        self.resolution_combo.set_active(0)
        self.resolution_combo.connect("changed", self._on_resolution_changed)
        box.pack_start(self.resolution_combo, False, False, 0)

        # Custom resolution spin buttons
        self.custom_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.width_spin = Gtk.SpinButton.new_with_range(640, 7680, 1)
        self.width_spin.set_value(1920)
        self.height_spin = Gtk.SpinButton.new_with_range(480, 4320, 1)
        self.height_spin.set_value(1080)
        self.custom_box.pack_start(self.width_spin, True, True, 0)
        self.custom_box.pack_start(Gtk.Label(label="x"), False, False, 0)
        self.custom_box.pack_start(self.height_spin, True, True, 0)
        box.pack_start(self.custom_box, False, False, 0)
        self.custom_box.set_no_show_all(True)

        # Memory gsettings toggle
        self.memory_backend_check = Gtk.CheckButton(label="Use memory gsettings backend")
        self.memory_backend_check.set_active(True)
        box.pack_start(self.memory_backend_check, False, False, 0)

        # Command entry
        cmd_label = Gtk.Label(label="Command", xalign=0)
        box.pack_start(cmd_label, False, False, 0)

        self.command_entry = Gtk.Entry()
        self.command_entry.set_text(DEFAULT_COMMAND)
        box.pack_start(self.command_entry, False, False, 0)

        # Launch button
        self.launch_button = Gtk.Button(label="Launch")
        self.launch_button.connect("clicked", self._on_launch_clicked)
        box.pack_start(self.launch_button, False, False, 0)

    def _on_resolution_changed(self, combo):
        is_custom = combo.get_active_text() == "Custom"
        if is_custom:
            self.custom_box.show_all()
        else:
            self.custom_box.hide()

    def _get_resolution(self):
        text = self.resolution_combo.get_active_text()

        if text == "Full workarea":
            display = Gdk.Display.get_default()
            window = self.get_window()
            monitor = display.get_monitor_at_window(window)
            workarea = monitor.get_workarea()
            return workarea.width, workarea.height - TITLEBAR_HEIGHT

        if text == "Custom":
            return int(self.width_spin.get_value()), int(self.height_spin.get_value())

        # Preset like "1920x1080"
        w, h = text.split("x")
        return int(w), int(h)

    def _on_launch_clicked(self, button):
        width, height = self._get_resolution()
        command = self.command_entry.get_text().strip()
        if not command:
            return

        env = os.environ.copy()
        env["MUFFIN_DEBUG_DUMMY_MODE_SPECS"] = f"{width}x{height}"

        if self.memory_backend_check.get_active():
            env["GSETTINGS_BACKEND"] = "memory"

        argv = ["dbus-run-session", "--"] + command.split()

        try:
            flags = GLib.SpawnFlags.SEARCH_PATH | GLib.SpawnFlags.DO_NOT_REAP_CHILD
            envlist = [f"{k}={v}" for k, v in env.items()]

            pid, _, _, _ = GLib.spawn_async(
                argv=argv,
                envp=envlist,
                flags=flags,
            )
            GLib.child_watch_add(GLib.PRIORITY_DEFAULT, pid, self._on_child_exit)
        except GLib.Error as e:
            dialog = Gtk.MessageDialog(
                transient_for=self,
                modal=True,
                message_type=Gtk.MessageType.ERROR,
                buttons=Gtk.ButtonsType.CLOSE,
                text=f"Failed to launch: {e.message}",
            )
            dialog.run()
            dialog.destroy()

    def _on_child_exit(self, pid, status):
        GLib.spawn_close_pid(pid)


def main():
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    win = NestedSessionLauncher()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()


if __name__ == "__main__":
    main()
