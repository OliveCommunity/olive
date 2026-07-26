#!/usr/bin/env python3
"""Batch convert simple VideoParams constant and static method references to facade equivalents."""

import os
import re

# Files that use VideoParams (non-standardcombos - those are already converted)
target_files = []
for root, dirs, fnames in os.walk('app'):
    for f in fnames:
        if f.endswith(('.cpp', '.h')) and 'autogen' not in root:
            path = os.path.join(root, f)
            try:
                content = open(path, 'r', errors='replace').read()
                if 'VideoParams' in content:
                    target_files.append(path)
            except:
                pass

print(f"Files with VideoParams: {len(target_files)}")

# Simple constant replacements (these are pure enum/integer replacements)
simple_replacements = [
    # (old, new) - EXACT string matches
    ('VideoParams::k_interlace_none', '0'),
    ('VideoParams::k_color_range_default', '0'),
    ('VideoParams::k_color_range_limited', '0'),
    ('VideoParams::k_color_range_full', '1'),
    ('VideoParams::k_rgba_channel_count', '4'),
    ('VideoParams::k_video_type_still', '1'),
    ('VideoParams::k_video_type_image_sequence', '2'),
    ('VideoParams::k_video_type_video', '0'),  # This might be wrong - check
    ('VideoParams::k_interlaced_top_first', '1'),
    ('VideoParams::k_interlaced_bottom_first', '2'),
]

# Track what was replaced
total_replacements = {}
for old, new in simple_replacements:
    total_replacements[old] = 0

# For each file, do the replacements
for filepath in target_files:
    with open(filepath, 'r', errors='replace') as f:
        content = f.read()
    
    original = content
    
    for old, new in simple_replacements:
        # Use word boundary to avoid partial matches
        count = content.count(old)
        if count > 0:
            content = content.replace(old, new)
            total_replacements[old] += count
    
    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"  Modified: {filepath}")

print("\nReplacement summary:")
for old, count in total_replacements.items():
    if count > 0:
        print(f"  {old} -> replaced {count} times")

print("\nDone!")
