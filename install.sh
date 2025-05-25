#!/bin/bash

# Muffin Tiling Gaps Installation Script
# Builds and installs tiling gaps functionality for Cinnamon

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
INSTALL_PREFIX="/usr/local"
BACKUP_DIR="$HOME/.muffin-gaps-backup"
BUILD_DIR="build"

# Functions
print_header() {
    echo -e "${BLUE}================================${NC}"
    echo -e "${BLUE}  Muffin Tiling Gaps Installer${NC}"
    echo -e "${BLUE}================================${NC}"
    echo ""
}

print_step() {
    echo -e "${GREEN}[STEP]${NC} $1"
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

check_dependencies() {
    print_step "Checking dependencies..."

    local missing_deps=()

    # Check build tools
    command -v meson >/dev/null 2>&1 || missing_deps+=("meson")
    command -v ninja >/dev/null 2>&1 || missing_deps+=("ninja")
    command -v gcc >/dev/null 2>&1 || missing_deps+=("gcc")
    command -v pkg-config >/dev/null 2>&1 || missing_deps+=("pkg-config")

    # Check runtime dependencies
    command -v python3 >/dev/null 2>&1 || missing_deps+=("python3")
    command -v gsettings >/dev/null 2>&1 || missing_deps+=("gsettings")

    # Check Python GTK bindings
    python3 -c "import gi; gi.require_version('Gtk', '3.0'); from gi.repository import Gtk" 2>/dev/null || missing_deps+=("python3-gi")

    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        echo ""
        print_info "Please install missing dependencies:"
        echo "  Ubuntu/Debian: sudo apt install meson ninja-build gcc pkg-config python3 python3-gi libglib2.0-dev libgtk-3-dev"
        echo "  Arch Linux: sudo pacman -S meson ninja gcc pkgconf python python-gobject gtk3"
        echo "  Fedora: sudo dnf install meson ninja-build gcc pkgconfig python3 python3-gobject gtk3-devel"
        exit 1
    fi

    print_success "All dependencies found"
}

check_cinnamon() {
    print_step "Checking Cinnamon environment..."

    if [ "$XDG_CURRENT_DESKTOP" != "X-Cinnamon" ]; then
        print_warning "Not running in Cinnamon desktop environment"
        print_info "This tool is designed for Cinnamon. Continue anyway? (y/N)"
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi

    # Check if muffin is running
    if ! pgrep -x "cinnamon" > /dev/null; then
        print_warning "Cinnamon is not running"
    else
        print_success "Cinnamon environment detected"
    fi
}

create_backup() {
    print_step "Creating backup of original Muffin library..."

    mkdir -p "$BACKUP_DIR"

    # Backup system library
    if [ -f "/usr/lib/libmuffin.so.0.0.0" ]; then
        sudo cp "/usr/lib/libmuffin.so.0.0.0" "$BACKUP_DIR/libmuffin.so.0.0.0.system"
        print_success "System library backed up to $BACKUP_DIR"
    else
        print_warning "System Muffin library not found at /usr/lib/libmuffin.so.0.0.0"
    fi

    # Save installation info
    echo "$(date): Muffin Tiling Gaps installation" > "$BACKUP_DIR/install_info.txt"
    echo "Original library: /usr/lib/libmuffin.so.0.0.0" >> "$BACKUP_DIR/install_info.txt"
    echo "Backup location: $BACKUP_DIR/libmuffin.so.0.0.0.system" >> "$BACKUP_DIR/install_info.txt"
}

build_muffin() {
    print_step "Building Muffin with tiling gaps..."

    # Clean previous build
    if [ -d "$BUILD_DIR" ]; then
        print_info "Cleaning previous build..."
        rm -rf "$BUILD_DIR"
    fi

    # Configure build
    print_info "Configuring build with meson..."
    meson "$BUILD_DIR" --prefix="$INSTALL_PREFIX"

    # Build
    print_info "Compiling with ninja..."
    ninja -C "$BUILD_DIR"

    print_success "Build completed successfully"
}

install_muffin() {
    print_step "Installing Muffin with tiling gaps..."

    # Install to prefix
    print_info "Installing to $INSTALL_PREFIX..."
    sudo ninja -C "$BUILD_DIR" install

    # Copy library to system location for Cinnamon to use
    print_info "Installing library for Cinnamon..."
    if [ -f "$BUILD_DIR/src/libmuffin.so.0.0.0" ]; then
        sudo cp "$BUILD_DIR/src/libmuffin.so.0.0.0" "/usr/lib/libmuffin.so.0.0.0"
        print_success "Library installed successfully"
    else
        print_error "Built library not found!"
        exit 1
    fi
}

install_schema() {
    print_step "Installing GSettings schema..."

    # Compile schemas
    print_info "Compiling GSettings schemas..."
    sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

    print_success "Schema installed and compiled"
}

create_tools() {
    print_step "Creating configuration tools..."

    # Ensure tools directory exists
    if [ ! -d "tools" ]; then
        print_info "Creating tools directory..."
        mkdir -p tools
    fi

    # Create muffin-gaps command line tool
    cat > tools/muffin-gaps << 'EOF'
#!/bin/bash

# Muffin Tiling Gaps Control Script
# Provides command-line interface for configuring tiling gaps

# Function to show usage
show_usage() {
    echo "Muffin Tiling Gaps Control"
    echo ""
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "    enable              Enable tiling gaps"
    echo "    disable             Disable tiling gaps"
    echo "    status              Show current gap settings"
    echo "    set-inner SIZE      Set inner gap size (0-100)"
    echo "    set-outer SIZE      Set outer gap size (0-100)"
    echo "    reset               Reset all gap settings to defaults"
    echo "    gui                 Launch graphical configuration tool"
    echo ""
    echo "Examples:"
    echo "    $0 enable                    # Enable tiling gaps"
    echo "    $0 set-inner 15             # Set inner gap to 15 pixels"
    echo "    $0 set-outer 10             # Set outer gap to 10 pixels"
    echo "    $0 status                   # Show current settings"
    echo ""
}

# Function to validate gap size
validate_size() {
    local size=$1
    if ! [[ "$size" =~ ^[0-9]+$ ]] || [ "$size" -lt 0 ] || [ "$size" -gt 100 ]; then
        echo "Error: Gap size must be a number between 0 and 100"
        exit 1
    fi
}

# Function to enable gaps
enable_gaps() {
    gsettings set org.cinnamon.muffin tiling-gaps-enabled true
    echo "Tiling gaps enabled"
}

# Function to disable gaps
disable_gaps() {
    gsettings set org.cinnamon.muffin tiling-gaps-enabled false
    echo "Tiling gaps disabled"
}

# Function to show status
show_status() {
    local enabled=$(gsettings get org.cinnamon.muffin tiling-gaps-enabled)
    local inner_gap=$(gsettings get org.cinnamon.muffin tiling-gap-size)
    local outer_gap=$(gsettings get org.cinnamon.muffin tiling-outer-gap-size)

    echo "Tiling Gaps Status:"
    echo "  Enabled: $enabled"
    echo "  Inner gap size: $inner_gap pixels"
    echo "  Outer gap size: $outer_gap pixels"
}

# Function to set inner gap
set_inner_gap() {
    local size=$1
    validate_size "$size"
    gsettings set org.cinnamon.muffin tiling-gap-size "$size"
    echo "Inner gap size set to $size pixels"
}

# Function to set outer gap
set_outer_gap() {
    local size=$1
    validate_size "$size"
    gsettings set org.cinnamon.muffin tiling-outer-gap-size "$size"
    echo "Outer gap size set to $size pixels"
}

# Function to reset settings
reset_settings() {
    gsettings reset org.cinnamon.muffin tiling-gaps-enabled
    gsettings reset org.cinnamon.muffin tiling-gap-size
    gsettings reset org.cinnamon.muffin tiling-outer-gap-size
    echo "Gap settings reset to defaults"
}

# Function to launch GUI
launch_gui() {
    if [ -f "$(dirname "$0")/tiling-gaps-config.py" ]; then
        python3 "$(dirname "$0")/tiling-gaps-config.py"
    else
        echo "Error: GUI configuration tool not found"
        exit 1
    fi
}

# Main script logic
case "$1" in
    enable)
        enable_gaps
        ;;
    disable)
        disable_gaps
        ;;
    status)
        show_status
        ;;
    set-inner)
        if [ -z "$2" ]; then
            echo "Error: Please specify gap size"
            echo "Usage: $0 set-inner SIZE"
            exit 1
        fi
        set_inner_gap "$2"
        ;;
    set-outer)
        if [ -z "$2" ]; then
            echo "Error: Please specify gap size"
            echo "Usage: $0 set-outer SIZE"
            exit 1
        fi
        set_outer_gap "$2"
        ;;
    reset)
        reset_settings
        ;;
    gui)
        launch_gui
        ;;
    help|--help|-h)
        show_usage
        ;;
    "")
        show_usage
        ;;
    *)
        echo "Error: Unknown command '$1'"
        echo ""
        show_usage
        exit 1
        ;;
esac
EOF

    print_info "Created command line tool: tools/muffin-gaps"

    # Create GUI configuration tool
    cat > tools/tiling-gaps-config.py << 'EOF'
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
EOF

    print_info "Created GUI configuration tool: tools/tiling-gaps-config.py"

    # Create test script
    cat > tools/test-tiling-gaps.py << 'EOF'
#!/usr/bin/env python3
"""
Test script to verify tiling gaps functionality
"""

import subprocess
import time
import sys
import os

def run_command(cmd):
    """Run a shell command and return the result"""
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        return result.returncode == 0, result.stdout, result.stderr
    except Exception as e:
        return False, "", str(e)

def verify_installation():
    """Verify that our tiling gaps implementation is working"""
    print("=== Verifying Tiling Gaps Installation ===\n")

    # Check if our GSettings keys exist
    tests = [
        ("tiling-gaps-enabled", "boolean"),
        ("tiling-gap-size", "integer"),
        ("tiling-outer-gap-size", "integer")
    ]

    all_passed = True

    for key, key_type in tests:
        success, output, error = run_command(f"gsettings get org.cinnamon.muffin {key}")
        if success:
            print(f"✓ {key}: {output.strip()}")
        else:
            print(f"✗ {key}: Failed to read ({error})")
            all_passed = False

    if all_passed:
        print("\n✓ All tiling gaps settings are available!")
        print("✓ Installation appears successful!")
        return True
    else:
        print("\n✗ Some settings are missing. Installation may have failed.")
        return False

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--verify":
        verify_installation()
    else:
        verify_installation()
EOF

    print_info "Created test script: tools/test-tiling-gaps.py"
}

setup_tools() {
    print_step "Setting up configuration tools..."

    # Create tools if they don't exist
    create_tools

    # Make tools executable
    chmod +x tools/muffin-gaps
    chmod +x tools/tiling-gaps-config.py
    chmod +x tools/test-tiling-gaps.py

    # Make install/uninstall scripts executable and preserve them
    chmod +x install.sh uninstall.sh

    print_success "Configuration tools ready"
}

restart_cinnamon() {
    print_step "Restarting Cinnamon..."

    print_warning "Cinnamon will restart to load the new Muffin library"
    print_info "Your desktop will briefly disappear and reappear"
    print_info "Continue? (Y/n)"
    read -r response
    if [[ "$response" =~ ^[Nn]$ ]]; then
        print_info "Skipping Cinnamon restart"
        print_warning "You must restart Cinnamon manually for gaps to work:"
        print_info "  cinnamon --replace &"
        return
    fi

    # Kill existing Cinnamon
    killall cinnamon 2>/dev/null || true
    sleep 2

    # Start new Cinnamon
    cinnamon --replace &
    sleep 3

    print_success "Cinnamon restarted with tiling gaps support"
}

test_installation() {
    print_step "Testing installation..."

    # Test GSettings keys
    if gsettings get org.cinnamon.muffin tiling-gaps-enabled >/dev/null 2>&1; then
        print_success "GSettings integration working"
    else
        print_error "GSettings integration failed"
        return 1
    fi

    # Test command line tool
    if ./tools/muffin-gaps status >/dev/null 2>&1; then
        print_success "Command line tool working"
    else
        print_error "Command line tool failed"
        return 1
    fi

    print_success "Installation test passed"
}

show_usage() {
    print_step "Installation completed successfully!"
    echo ""
    print_info "How to use tiling gaps:"
    echo ""
    echo "  GUI Configuration:"
    echo "    python3 tools/tiling-gaps-config.py"
    echo ""
    echo "  Command Line:"
    echo "    ./tools/muffin-gaps enable"
    echo "    ./tools/muffin-gaps set-inner 15"
    echo "    ./tools/muffin-gaps set-outer 10"
    echo "    ./tools/muffin-gaps status"
    echo ""
    echo "  Testing:"
    echo "    1. Enable gaps: ./tools/muffin-gaps enable"
    echo "    2. Set gap sizes: ./tools/muffin-gaps set-outer 20"
    echo "    3. Drag windows to screen edges to tile them"
    echo "    4. Double-click title bars to maximize windows"
    echo ""
    print_info "Backup created at: $BACKUP_DIR"
    print_info "To uninstall, run: ./uninstall.sh"
    print_info "Install/uninstall scripts are preserved for future use"
}

# Main installation process
main() {
    print_header

    # Check if running as root
    if [ "$EUID" -eq 0 ]; then
        print_error "Do not run this script as root!"
        print_info "The script will ask for sudo when needed"
        exit 1
    fi

    # Check if we're in the right directory
    if [ ! -f "meson.build" ] || [ ! -d "src" ]; then
        print_error "Please run this script from the muffin source directory"
        exit 1
    fi

    # Run installation steps
    check_dependencies
    check_cinnamon
    create_backup
    build_muffin
    install_muffin
    install_schema
    setup_tools
    restart_cinnamon
    test_installation
    show_usage

    print_success "Muffin Tiling Gaps installation completed!"
}

# Run main function
main "$@"
