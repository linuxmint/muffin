#!/usr/bin/env python3
"""
Muffin Tiling Gaps Configuration GUI
Provides a graphical interface for configuring tiling gaps
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gio

class TilingGapsConfig:
    def __init__(self):
        # Initialize GSettings
        self.settings = Gio.Settings.new("org.cinnamon.muffin")

        # Create main window
        self.window = Gtk.Window()
        self.window.set_title("Muffin Tiling Gaps Configuration")
        self.window.set_default_size(400, 300)
        self.window.set_resizable(False)
        self.window.connect("destroy", Gtk.main_quit)

        # Create main container
        vbox = Gtk.VBox(spacing=10)
        vbox.set_margin_left(20)
        vbox.set_margin_right(20)
        vbox.set_margin_top(20)
        vbox.set_margin_bottom(20)
        self.window.add(vbox)

        # Title
        title_label = Gtk.Label()
        title_label.set_markup("<b>Tiling Gaps Configuration</b>")
        vbox.pack_start(title_label, False, False, 0)

        # Enable/disable checkbox
        self.enable_checkbox = Gtk.CheckButton(label="Enable Tiling Gaps")
        self.enable_checkbox.set_active(self.settings.get_boolean("tiling-gaps-enabled"))
        self.enable_checkbox.connect("toggled", self.on_enable_toggled)
        vbox.pack_start(self.enable_checkbox, False, False, 0)

        # Gap configuration frame
        gap_frame = Gtk.Frame(label="Gap Sizes")
        gap_vbox = Gtk.VBox(spacing=10)
        gap_vbox.set_margin_left(10)
        gap_vbox.set_margin_right(10)
        gap_vbox.set_margin_top(10)
        gap_vbox.set_margin_bottom(10)
        gap_frame.add(gap_vbox)

        # Inner gap configuration
        inner_hbox = Gtk.HBox(spacing=10)
        inner_label = Gtk.Label(label="Inner Gap Size:")
        inner_label.set_size_request(120, -1)
        inner_hbox.pack_start(inner_label, False, False, 0)

        self.inner_gap_scale = Gtk.HScale()
        self.inner_gap_scale.set_range(0, 100)
        self.inner_gap_scale.set_value(self.settings.get_int("tiling-gap-size"))
        self.inner_gap_scale.set_digits(0)
        self.inner_gap_scale.set_increments(1, 5)
        self.inner_gap_scale.connect("value-changed", self.on_inner_gap_changed)
        inner_hbox.pack_start(self.inner_gap_scale, True, True, 0)

        self.inner_gap_value = Gtk.Label(label=str(self.settings.get_int("tiling-gap-size")))
        self.inner_gap_value.set_size_request(30, -1)
        inner_hbox.pack_start(self.inner_gap_value, False, False, 0)

        gap_vbox.pack_start(inner_hbox, False, False, 0)

        # Outer gap configuration
        outer_hbox = Gtk.HBox(spacing=10)
        outer_label = Gtk.Label(label="Outer Gap Size:")
        outer_label.set_size_request(120, -1)
        outer_hbox.pack_start(outer_label, False, False, 0)

        self.outer_gap_scale = Gtk.HScale()
        self.outer_gap_scale.set_range(0, 100)
        self.outer_gap_scale.set_value(self.settings.get_int("tiling-outer-gap-size"))
        self.outer_gap_scale.set_digits(0)
        self.outer_gap_scale.set_increments(1, 5)
        self.outer_gap_scale.connect("value-changed", self.on_outer_gap_changed)
        outer_hbox.pack_start(self.outer_gap_scale, True, True, 0)

        self.outer_gap_value = Gtk.Label(label=str(self.settings.get_int("tiling-outer-gap-size")))
        self.outer_gap_value.set_size_request(30, -1)
        outer_hbox.pack_start(self.outer_gap_value, False, False, 0)

        gap_vbox.pack_start(outer_hbox, False, False, 0)

        vbox.pack_start(gap_frame, False, False, 0)

        # Description
        desc_label = Gtk.Label()
        desc_label.set_markup(
            "<i>Inner gaps appear between tiled windows.\n"
            "Outer gaps appear around the screen edges.</i>"
        )
        desc_label.set_line_wrap(True)
        vbox.pack_start(desc_label, False, False, 0)

        # Buttons
        button_hbox = Gtk.HBox(spacing=10)

        reset_button = Gtk.Button(label="Reset to Defaults")
        reset_button.connect("clicked", self.on_reset_clicked)
        button_hbox.pack_start(reset_button, False, False, 0)

        close_button = Gtk.Button(label="Close")
        close_button.connect("clicked", lambda _: Gtk.main_quit())
        button_hbox.pack_end(close_button, False, False, 0)

        vbox.pack_end(button_hbox, False, False, 0)

        # Update sensitivity based on enable state
        self.update_sensitivity()

    def on_enable_toggled(self, checkbox):
        enabled = checkbox.get_active()
        self.settings.set_boolean("tiling-gaps-enabled", enabled)
        self.update_sensitivity()

    def on_inner_gap_changed(self, scale):
        value = int(scale.get_value())
        self.settings.set_int("tiling-gap-size", value)
        self.inner_gap_value.set_text(str(value))

    def on_outer_gap_changed(self, scale):
        value = int(scale.get_value())
        self.settings.set_int("tiling-outer-gap-size", value)
        self.outer_gap_value.set_text(str(value))

    def on_reset_clicked(self, _button):
        self.settings.reset("tiling-gaps-enabled")
        self.settings.reset("tiling-gap-size")
        self.settings.reset("tiling-outer-gap-size")

        # Update UI
        self.enable_checkbox.set_active(self.settings.get_boolean("tiling-gaps-enabled"))
        self.inner_gap_scale.set_value(self.settings.get_int("tiling-gap-size"))
        self.outer_gap_scale.set_value(self.settings.get_int("tiling-outer-gap-size"))
        self.inner_gap_value.set_text(str(self.settings.get_int("tiling-gap-size")))
        self.outer_gap_value.set_text(str(self.settings.get_int("tiling-outer-gap-size")))
        self.update_sensitivity()

    def update_sensitivity(self):
        enabled = self.enable_checkbox.get_active()
        self.inner_gap_scale.set_sensitive(enabled)
        self.outer_gap_scale.set_sensitive(enabled)

    def run(self):
        self.window.show_all()
        Gtk.main()

if __name__ == "__main__":
    app = TilingGapsConfig()
    app.run()
