# Read-only: display interrupt registers (never writes). Run as root.
import mmap, os, struct
fd = os.open('/sys/bus/pci/devices/0000:00:02.0/resource0', os.O_RDONLY)
m = mmap.mmap(fd, 0x200000, mmap.MAP_SHARED, mmap.PROT_READ)
r = lambda o: struct.unpack('<I', m[o:o+4])[0]
for n, o in [('GEN8_MASTER_IRQ',0x44200),('VLV_IIR_RW',0x182084),('VLV_IER',0x1820a0),('VLV_IIR',0x1820a4),
             ('VLV_IMR',0x1820a8),('VLV_ISR',0x1820ac),('PIPEASTAT',0x1f0024),('PIPEBSTAT',0x1f1024),('PIPECSTAT',0x1f3024),
             ('PORT_HOTPLUG_EN',0x182110),('PORT_HOTPLUG_STAT',0x182114),('DPINVGTT',0x1b0038),('PIPEACONF',0x1f0008),('PIPECCONF',0x1f3008)]:
    print(f'{n:18} {o:06x} {r(o):08x}')
