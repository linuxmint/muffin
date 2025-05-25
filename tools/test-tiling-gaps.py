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
