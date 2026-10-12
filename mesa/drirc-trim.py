#!/usr/bin/env python3
"""Trim Mesa's 00-mesa-defaults.conf for the GPD (run by PKGBUILD package_mesa-gpd).

Every GL process parses the drirc files at start (R7: ~6.5 ms of ~51 ms). Most of the per-application
entries name native Linux games/tools that are not on this device. Keep: the device-wide options,
<engine> entries, every Windows .exe entry (Wine games, including ones installed later), entries
matched by regexp or anything other than a plain executable name, and native executables listed in
drirc-keep.txt. Drop the other native-executable entries.
  drirc-trim.py IN OUT KEEPLIST
"""
import sys
import xml.etree.ElementTree as ET

src, dst, keepfile = sys.argv[1:4]
keep = {l.strip() for l in open(keepfile) if l.strip() and not l.startswith('#')}
tree = ET.parse(src)
dropped = kept = 0
for device in tree.getroot().iter('device'):
    for app in list(device.findall('application')):
        exe = app.get('executable')
        plain_native = (exe is not None and not exe.lower().endswith('.exe') and exe not in keep
                        and set(app.keys()) <= {'name', 'executable'})
        if plain_native:
            device.remove(app)
            dropped += 1
        else:
            kept += 1
tree.write(dst, encoding='utf-8', xml_declaration=True)
print(f'drirc-trim: kept {kept} application entries, dropped {dropped}', file=sys.stderr)
