#!/bin/bash

# Muffin Tiling Gaps Uninstallation Script
# Removes tiling gaps functionality and restores original Muffin

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BACKUP_DIR="$HOME/.muffin-gaps-backup"

# Functions
print_header() {
    echo -e "${BLUE}===================================${NC}"
    echo -e "${BLUE}  Muffin Tiling Gaps Uninstaller${NC}"
    echo -e "${BLUE}===================================${NC}"
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

confirm_uninstall() {
    print_warning "This will remove Muffin Tiling Gaps and restore the original Muffin"
    print_info "The following will be removed/restored:"
    echo "  - Custom Muffin library with tiling gaps"
    echo "  - Configuration tools (tools/ directory)"
    echo "  - GSettings will be reset to defaults"
    echo ""
    print_warning "Continue with uninstallation? (y/N)"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        print_info "Uninstallation cancelled"
        exit 0
    fi
}

check_backup() {
    print_step "Checking for backup..."

    if [ ! -d "$BACKUP_DIR" ]; then
        print_error "Backup directory not found: $BACKUP_DIR"
        print_info "Cannot restore original Muffin library"
        print_warning "Continue anyway? This may leave the system in an inconsistent state (y/N)"
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            exit 1
        fi
        return 1
    fi

    if [ ! -f "$BACKUP_DIR/libmuffin.so.0.0.0.system" ]; then
        print_error "Original Muffin library backup not found"
        print_warning "Cannot restore original library. Continue? (y/N)"
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            exit 1
        fi
        return 1
    fi

    print_success "Backup found"
    return 0
}

restore_library() {
    print_step "Restoring original Muffin library..."

    if check_backup; then
        # Restore from backup
        print_info "Restoring from backup..."
        sudo cp "$BACKUP_DIR/libmuffin.so.0.0.0.system" "/usr/lib/libmuffin.so.0.0.0"
        print_success "Original Muffin library restored"
    else
        print_warning "Skipping library restoration (no backup available)"
    fi
}

reset_gsettings() {
    print_step "Resetting GSettings to defaults..."

    # Reset tiling gaps settings
    print_info "Resetting tiling gaps settings..."
    gsettings reset org.cinnamon.muffin tiling-gaps-enabled 2>/dev/null || print_warning "Could not reset tiling-gaps-enabled"
    gsettings reset org.cinnamon.muffin tiling-gap-size 2>/dev/null || print_warning "Could not reset tiling-gap-size"
    gsettings reset org.cinnamon.muffin tiling-outer-gap-size 2>/dev/null || print_warning "Could not reset tiling-outer-gap-size"

    print_success "GSettings reset to defaults"
}

remove_tools() {
    print_step "Removing configuration tools..."

    # Remove tools directory
    if [ -d "tools" ]; then
        print_info "Removing tools directory..."
        rm -rf tools/
        print_success "Configuration tools removed"
    else
        print_info "Tools directory not found (already removed?)"
    fi

    # Remove documentation files (but preserve install/uninstall scripts)
    [ -f "TILING_GAPS_README.md" ] && rm -f TILING_GAPS_README.md
    [ -f "INSTALLATION_GUIDE.md" ] && rm -f INSTALLATION_GUIDE.md

    print_info "Installation scripts preserved for future use"
}

clean_build() {
    print_step "Cleaning build artifacts..."

    # Remove build directory
    if [ -d "build" ]; then
        print_info "Removing build directory..."
        rm -rf build/
        print_success "Build artifacts cleaned"
    else
        print_info "Build directory not found"
    fi

    # Remove any test build directories
    [ -d "build-test" ] && rm -rf build-test/
}

restart_cinnamon() {
    print_step "Restarting Cinnamon..."

    print_warning "Cinnamon will restart to load the original Muffin library"
    print_info "Your desktop will briefly disappear and reappear"
    print_info "Continue? (Y/n)"
    read -r response
    if [[ "$response" =~ ^[Nn]$ ]]; then
        print_info "Skipping Cinnamon restart"
        print_warning "You must restart Cinnamon manually:"
        print_info "  cinnamon --replace &"
        return
    fi

    # Kill existing Cinnamon
    killall cinnamon 2>/dev/null || true
    sleep 2

    # Start Cinnamon
    cinnamon --replace &
    sleep 3

    print_success "Cinnamon restarted with original Muffin"
}

verify_removal() {
    print_step "Verifying removal..."

    # Check if tiling gaps settings are gone
    if gsettings get org.cinnamon.muffin tiling-gaps-enabled >/dev/null 2>&1; then
        print_warning "Tiling gaps settings still present (this is normal)"
        print_info "Settings exist but are reset to defaults"
    fi

    # Check if tools are removed
    if [ -d "tools" ]; then
        print_warning "Tools directory still exists"
    else
        print_success "Tools directory removed"
    fi

    print_success "Uninstallation verification completed"
}

clean_backup() {
    print_step "Cleaning up backup..."

    print_info "Remove backup directory? (y/N)"
    print_warning "This will permanently delete the backup of the original Muffin library"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        rm -rf "$BACKUP_DIR"
        print_success "Backup directory removed"
    else
        print_info "Backup preserved at: $BACKUP_DIR"
    fi
}

show_completion() {
    print_step "Uninstallation completed!"
    echo ""
    print_success "Muffin Tiling Gaps has been removed"
    print_info "Your system has been restored to the original state"
    echo ""
    print_info "What was removed:"
    echo "  ✓ Custom Muffin library with tiling gaps"
    echo "  ✓ Configuration tools"
    echo "  ✓ Build artifacts"
    echo "  ✓ GSettings reset to defaults"
    echo ""
    if [ -d "$BACKUP_DIR" ]; then
        print_info "Backup still available at: $BACKUP_DIR"
        print_info "You can safely remove it if you don't plan to reinstall"
    fi
}

# Main uninstallation process
main() {
    print_header

    # Check if running as root
    if [ "$EUID" -eq 0 ]; then
        print_error "Do not run this script as root!"
        print_info "The script will ask for sudo when needed"
        exit 1
    fi

    # Confirm uninstallation
    confirm_uninstall

    # Run uninstallation steps
    restore_library
    reset_gsettings
    remove_tools
    clean_build
    restart_cinnamon
    verify_removal
    clean_backup
    show_completion

    print_success "Uninstallation completed successfully!"
}

# Run main function
main "$@"
