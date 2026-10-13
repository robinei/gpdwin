import struct, sys
d = open(sys.argv[1], "rb").read()
i = d.find(b"BIOS_DATA_BLOCK ")
bdb_hdr = struct.unpack_from("<H", d, i+18)[0]   # header size at +18? use known: sig16, ver2, hdrsize2, bdbsize2
sig, ver, hsz, bsz = struct.unpack_from("<16sHHH", d, i)
p = i + hsz
blocks = {}
while p < i + bsz:
    bid = d[p]; size = struct.unpack_from("<H", d, p+1)[0]
    blocks[bid] = d[p+3:p+3+size]
    p += 3 + size
b = blocks[52]
print("block52 size", len(b), "panel_type?")
# panel index from block 40
b40 = blocks[40]
print("block40 panel_type byte:", b40[0])
cfg = b[:137]
off = 0
panel_id, = struct.unpack_from("<H", cfg, 0)
gen, = struct.unpack_from("<I", cfg, 2)
port, = struct.unpack_from("<H", cfg, 6)
ctrl, = struct.unpack_from("<H", cfg, 8)
rsvd5 = cfg[10]
tbf, ddr, bref = struct.unpack_from("<III", cfg, 11)
byte_clk_sel = cfg[23]
dphy_flags, = struct.unpack_from("<H", cfg, 24)
hs, lp, ta, rst, mit, bw, lpb = struct.unpack_from("<IIIIIII", cfg, 26)
dphy, = struct.unpack_from("<I", cfg, 54)
clksw, hlsw = struct.unpack_from("<II", cfg, 58)
t = struct.unpack_from("<BBBBBBBBHBBBBBHBBBBBB", cfg, 90)
names = "tclk_miss tclk_post rsvd12 tclk_pre tclk_prepare tclk_settle tclk_term_enable tclk_trail tclk_prepare_clkzero rsvd13 td_term_enable teot ths_exit ths_prepare ths_prepare_hszero rsvd14 ths_settle ths_skip ths_trail tinit tlpx".split()
print("panel_id", panel_id)
print("general 0x%08x: dither=%d bridge=%d arch=%d cmd=%d vmode=%d cabc=%d pwm_blc=%d fmt=%d rot=%d bta_disable=%d" % (gen, gen&1, (gen>>2)&1, (gen>>3)&3, (gen>>5)&1, (gen>>6)&3, (gen>>8)&1, (gen>>9)&1, (gen>>10)&0xf, (gen>>14)&3, (gen>>16)&1))
print("port 0x%04x: dual_link=%d lane_cnt=%d(+1) overlap=%d rgb_flip=%d cabc_ports=%d bl_ports=%d" % (port, port&3, (port>>2)&3, (port>>4)&7, (port>>7)&1, (port>>8)&3, (port>>10)&3))
print("target_burst %d ddr %d bref %d byte_clk_sel 0x%02x (sel=%d)" % (tbf, ddr, bref, byte_clk_sel, byte_clk_sel&3))
print("dphy_flags 0x%04x: valid=%d eot_disabled=%d clk_stop=%d blank_pkts=%d lp_clk_lpm=%d" % (dphy_flags, dphy_flags&1, (dphy_flags>>1)&1, (dphy_flags>>2)&1, (dphy_flags>>3)&1, (dphy_flags>>4)&1))
print("hs_tx 0x%x lp_rx 0x%x ta 0x%x rst 0x%x master_init 0x%x bw 0x%x lp_byte_clk 0x%x" % (hs, lp, ta, rst, mit, bw, lpb))
print("dphy params 0x%08x clk_lane_switch 0x%x hl_switch 0x%x" % (dphy, clksw, hlsw))
for n, v in zip(names, t): print("  %s = %d (0x%x)" % (n, v, v))
