#!/usr/bin/env python3
"""
Test client for the wlr-foreign-toplevel-management protocol.

Connects to the Wayland compositor, binds the zwlr_foreign_toplevel_manager_v1
global, and displays a GTK window listing all toplevel windows with action
buttons (maximize, minimize, activate, close, fullscreen).

Requires: python3, python3-gi, GTK3, wl_framework
  (clone https://codeberg.org/Consolatis/wl_framework)

Usage: PYTHONPATH=/path/to/wl_framework WAYLAND_DISPLAY=wayland-0 python3 test-foreign-toplevel.py
"""

import sys
import os

# Allow running from the dev tree
WL_FRAMEWORK_PATH = os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', 'wl_framework')
if os.path.isdir(WL_FRAMEWORK_PATH):
    sys.path.insert(0, os.path.abspath(WL_FRAMEWORK_PATH))

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib, GObject, Pango

from wl_framework.loop_integrations import GLibIntegration
from wl_framework.network.connection import WaylandConnection
from wl_framework.protocols.foreign_toplevel import ForeignTopLevel


class ToplevelManager(ForeignTopLevel):
    def __init__(self, wl_connection, context):
        super().__init__(wl_connection)
        self.context = context

    def on_toplevel_created(self, toplevel):
        self.context.emit('toplevel-new', toplevel)

    def on_toplevel_synced(self, toplevel):
        self.context.emit('toplevel-synced', toplevel)

    def on_toplevel_closed(self, toplevel):
        self.context.emit('toplevel-closed', toplevel)


class Context(GObject.Object, WaylandConnection):
    __gsignals__ = {
        'toplevel-new':    (GObject.SignalFlags.RUN_LAST, GObject.TYPE_PYOBJECT, (GObject.TYPE_PYOBJECT,)),
        'toplevel-synced': (GObject.SignalFlags.RUN_LAST, GObject.TYPE_PYOBJECT, (GObject.TYPE_PYOBJECT,)),
        'toplevel-closed': (GObject.SignalFlags.RUN_LAST, GObject.TYPE_PYOBJECT, (GObject.TYPE_PYOBJECT,)),
        'wl-ready':        (GObject.SignalFlags.RUN_LAST, GObject.TYPE_PYOBJECT, ()),
    }

    def __init__(self):
        GObject.Object.__init__(self)
        WaylandConnection.__init__(self, eventloop_integration=GLibIntegration())
        self.seat = None
        self.manager = None

    def on_initial_sync(self, data):
        super().on_initial_sync(data)
        self.seat = self.display.seat
        self.manager = ToplevelManager(self, self)
        self.emit('wl-ready')


class PanelUI:
    def __init__(self, context):
        self.context = context
        self.rows = {}

        context.connect('toplevel-new', self._on_new)
        context.connect('toplevel-synced', self._on_synced)
        context.connect('toplevel-closed', self._on_closed)
        context.connect('wl-ready', self._on_ready)

        self._build_ui()

    def _build_ui(self):
        self.window = Gtk.Window(title="Foreign Toplevel Test")
        self.window.set_default_size(750, 500)
        self.window.connect("destroy", Gtk.main_quit)

        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        vbox.set_margin_start(10)
        vbox.set_margin_end(10)
        vbox.set_margin_top(10)
        vbox.set_margin_bottom(10)
        self.window.add(vbox)

        self.status_label = Gtk.Label(label="Connecting to Wayland...")
        self.status_label.set_xalign(0)
        vbox.pack_start(self.status_label, False, False, 0)

        vbox.pack_start(Gtk.Separator(), False, False, 0)

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        vbox.pack_start(scroll, True, True, 0)

        self.listbox = Gtk.ListBox()
        self.listbox.set_selection_mode(Gtk.SelectionMode.NONE)
        scroll.add(self.listbox)

        self.window.show_all()

    def _on_ready(self, context):
        self.status_label.set_text("Connected - bound foreign-toplevel-manager v%d" %
                                   context.manager.version)

    def _on_new(self, context, toplevel):
        pass

    def _on_synced(self, context, toplevel):
        old_row = self.rows.get(toplevel.obj_id)
        if old_row:
            self.listbox.remove(old_row)

        row = self._create_row(toplevel)
        self.rows[toplevel.obj_id] = row
        self.listbox.add(row)

    def _on_closed(self, context, toplevel):
        row = self.rows.pop(toplevel.obj_id, None)
        if row:
            self.listbox.remove(row)

    def _state_str(self, toplevel):
        return ", ".join(toplevel.states) if toplevel.states else "normal"

    def _create_row(self, toplevel):
        row = Gtk.ListBoxRow()
        hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        hbox.set_margin_start(6)
        hbox.set_margin_end(6)
        hbox.set_margin_top(4)
        hbox.set_margin_bottom(4)
        row.add(hbox)

        info_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        hbox.pack_start(info_box, True, True, 0)

        title_label = Gtk.Label()
        title_label.set_xalign(0)
        title_label.set_ellipsize(Pango.EllipsizeMode.END)
        title_label.set_markup(
            "<b>%s</b>" % GLib.markup_escape_text(toplevel.title or "(no title)"))
        info_box.pack_start(title_label, False, False, 0)

        detail_label = Gtk.Label()
        detail_label.set_xalign(0)
        detail_label.set_markup(
            "<small>app_id: %s  |  state: %s  |  handle: %d</small>" % (
                GLib.markup_escape_text(toplevel.app_id or "(none)"),
                self._state_str(toplevel),
                toplevel.obj_id))
        info_box.pack_start(detail_label, False, False, 0)

        btn_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        hbox.pack_end(btn_box, False, False, 0)

        self._add_button(btn_box, "Activate",
                         lambda b: toplevel.activate(self.context.seat))

        if 'maximized' in toplevel.states:
            self._add_button(btn_box, "Unmax",
                             lambda b: toplevel.set_maximize(False))
        else:
            self._add_button(btn_box, "Max",
                             lambda b: toplevel.set_maximize(True))

        if 'minimized' in toplevel.states:
            self._add_button(btn_box, "Unmin",
                             lambda b: toplevel.set_minimize(False))
        else:
            self._add_button(btn_box, "Min",
                             lambda b: toplevel.set_minimize(True))

        if 'fullscreen' in toplevel.states:
            self._add_button(btn_box, "Unfs",
                             lambda b: toplevel.set_fullscreen(False))
        else:
            self._add_button(btn_box, "Full",
                             lambda b: toplevel.set_fullscreen(True))

        btn_close = self._add_button(btn_box, "Close",
                                     lambda b: toplevel.close())
        btn_close.get_style_context().add_class("destructive-action")

        row.show_all()
        return row

    def _add_button(self, box, label, callback):
        btn = Gtk.Button(label=label)
        btn.connect("clicked", callback)
        box.pack_start(btn, False, False, 0)
        return btn


def main():
    try:
        context = Context()
    except RuntimeError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    ui = PanelUI(context)

    try:
        Gtk.main()
    except KeyboardInterrupt:
        print()


if __name__ == '__main__':
    main()
