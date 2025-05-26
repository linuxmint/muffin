# Muffin Tiling Gaps Patch

## 1. What is Muffin?

Muffin is the window manager for the Cinnamon desktop environment (used by Linux Mint and others). It controls how application windows are arranged, tiled, moved, and displayed on your screen.

## 2. What is this Patch? (Features)

This patch adds **native tiling gaps support** to Muffin. Tiling gaps are the spaces between windows when you use window tiling (snapping windows to the left, right, corners, etc.). With this patch, you can:

- Set the size of gaps between tiled windows
- Have separate gap settings for maximized windows
- Enjoy a more visually pleasing and customizable tiling experience

**Key Features:**

- Native gaps: Gaps are handled by Muffin itself, not by an extension workaround
- Configurable: Set your preferred gap size for both tiling and maximized windows
- Reliable: No visual glitches or race conditions that can happen with extension-only solutions
- System-wide: Once installed, the patch works for all users and sessions

## 3. Why Patch Muffin Instead of Using an Extension?

Extensions can only simulate gaps by moving/resizing windows after tiling, which is less reliable and can cause flicker or glitches. Only a Muffin patch can provide true, robust, and seamless tiling gaps for all tiling and maximize actions. With this patch, gaps are applied at the window manager level, so all tiling actions (including those from keyboard shortcuts, mouse, or other tools) will have consistent gaps.

## 4. Requirements

- A Linux system running Cinnamon (Linux Mint, Ubuntu Cinnamon, etc.)
- Ability to build and install software from source (requires sudo privileges)
- Comfortable using the terminal

## 5. Dependencies

Before installing the patch, install the following packages for your distribution:

### Ubuntu/Debian

```zsh
sudo apt update
sudo apt install build-essential meson ninja-build gettext libglib2.0-dev libmuffin-dev libgtk-3-dev libgirepository1.0-dev libjson-glib-dev libx11-dev libxcomposite-dev libxdamage-dev libxext-dev libxfixes-dev libxi-dev libxinerama-dev libxrandr-dev libxrender-dev libxres-dev libxtst-dev libstartup-notification0-dev libsm-dev libice-dev libcanberra-gtk3-dev libdbus-1-dev libsystemd-dev libgudev-1.0-dev libinput-dev libudev-dev libxkbcommon-dev libxkbfile-dev libxkbregistry-dev libxkbcommon-x11-dev libwayland-dev libwayland-egl-backend-dev libegl1-mesa-dev libgles2-mesa-dev libgbm-dev libdrm-dev libpam0g-dev libseccomp-dev libcap-dev
```

### Arch Linux

```zsh
sudo pacman -S base-devel meson ninja muffin gtk3 gobject-introspection json-glib libx11 libxcomposite libxdamage libxext libxfixes libxi libxinerama libxrandr libxrender libxres libxtst startup-notification libsm libice libcanberra-gtk3 dbus systemd libgudev libinput libudev xkbcommon xkbfile xkbregistry xkbcommon-x11 wayland wayland-egl mesa gbm libdrm pam libseccomp libcap
```

### Fedora

```zsh
sudo dnf install @development-tools meson ninja-build muffin-devel gtk3-devel gobject-introspection-devel json-glib-devel libX11-devel libXcomposite-devel libXdamage-devel libXext-devel libXfixes-devel libXi-devel libXinerama-devel libXrandr-devel libXrender-devel libXres-devel libXtst-devel startup-notification-devel libSM-devel libICE-devel libcanberra-gtk3-devel dbus-devel systemd-devel libgudev1-devel libinput-devel libudev-devel libxkbcommon-devel libxkbfile-devel libxkbregistry-devel libxkbcommon-x11-devel wayland-devel wayland-egl-devel mesa-libEGL-devel mesa-libGLES-devel mesa-libgbm-devel libdrm-devel pam-devel libseccomp-devel libcap-devel
```

## 6. How to Install, Run, and Uninstall

### Install the Patch

1. Open a terminal and navigate to the patch directory:

   ```zsh
   cd /path/to/tiling-project/library/muffin_mod
   ```

2. Run the install script:

   ```zsh
   ./install.sh
   ```

   The script will:
   - Backup your current Muffin installation
   - Build and install the patched Muffin
   - Compile GSettings schemas
   - Restart Cinnamon

### Using the Patched Muffin

- After installation, tiling gaps will be available system-wide.
- You can configure gap sizes using dconf-editor or compatible Cinnamon tools.
- All tiling and maximize actions will have consistent, native gaps.

### Uninstall (Restore Original Muffin)

1. If you want to revert to the original Muffin:

   ```zsh
   cd /path/to/tiling-project/library/muffin_mod
   ./uninstall.sh
   ```

   This will restore your backup and restart Cinnamon.

### After System Updates

- If a system update overwrites Muffin, simply re-run the install script to reapply the patch:

   ```zsh
   cd /path/to/tiling-project/library/muffin_mod
   ./install.sh
   ```

---

**For troubleshooting, see the logs produced by the install/uninstall scripts or contact the project maintainer.**
