# Read-only register snapshot of a stuck pipe B / MIPI port C (never writes). Run as root.
import mmap, os, struct, time
fd = os.open('/sys/bus/pci/devices/0000:00:02.0/resource0', os.O_RDONLY)
m = mmap.mmap(fd, 0x200000, mmap.MAP_SHARED, mmap.PROT_READ)
r = lambda o: struct.unpack('<I', m[o:o+4])[0]
names = {0x1f1000:'PIPEB_DSL',0x1f1008:'PIPEBCONF',0x1f1024:'PIPEBSTAT',0x1f1040:'PIPEB_FRMCNT',0x1f1044:'PIPEB_FLIPCNT',
 0x1f1180:'DSPBCNTR',0x1f119c:'DSPBSURF',0x1f11ac:'DSPBSURFLIVE',
 0x1e1000:'HTOTAL_B',0x1e100c:'VTOTAL_B',0x1e101c:'PIPESRC_B',0x1e1700:'MIPIC_PORT_CTRL',0x1e1190:'MIPIA_PORT_CTRL',
 0x18b000:'MIPIA_DEVICE_READY',0x18b004:'MIPIA_INTR_STAT'}
for rnd in range(3):
    print(f'--- t+{rnd*0.2:.1f}s')
    for o, n in names.items(): print(f'{n:18} {o:06x} {r(o):08x}')
    time.sleep(0.2)
print('--- MIPI C block 0x18b800..0x18b9ff')
for o in range(0x18b800, 0x18ba00, 16):
    print(f'{o:06x}: ' + ' '.join(f'{r(o+i):08x}' for i in range(0, 16, 4)))
