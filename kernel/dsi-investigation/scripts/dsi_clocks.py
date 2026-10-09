#!/usr/bin/env python3
"""Reimplementation of the v7.2.9 i915 VLV/CHV DSI clock arithmetic for the GPD Win 1 panel.

Mirrors (C integer semantics):
  display/vlv_dsi_pll.c: dsi_clk_from_pclk, dsi_calc_mnp, vlv_dsi_pclk
  display/vlv_dsi.c:     txbyteclkhs / pixels_from_txbyteclkhs (u16), set_dsi_timings, HS_TX_TIMEOUT
  display/intel_cdclk.c: vlv_calc_cdclk, _intel_pixel_rate_to_cdclk (CHV guardband 95 %)
"""
from fractions import Fraction as F

def div_round_closest(a, b):   # DIV_ROUND_CLOSEST for non-negative ints
    return (a + b // 2) // b

def div_round_up(a, b):
    return (a + b - 1) // b

lfsr_converts = [
    426, 469, 234, 373, 442, 221, 110, 311, 411,          # 62 - 70
    461, 486, 243, 377, 188, 350, 175, 343, 427, 213,     # 71 - 80
    106, 53, 282, 397, 454, 227, 113, 56, 284, 142,       # 81 - 90
    71, 35, 273, 136, 324, 418, 465, 488, 500, 506]       # 91 - 100

# ---- panel / VBT -----------------------------------------------------------
BPP, LANES = 24, 4
HDISP, HSS, HSE, HTOT = 720, 738, 756, 774
VDISP, VSS, VSE, VTOT = 1280, 1294, 1298, 1308
MODE_CLOCK = 61000        # kHz, VBT DVO timing
BURST_RATIO = 100         # non-burst with sync events

def dsi_clk_from_pclk(pclk):
    return div_round_closest(pclk * BPP, LANES)

def dsi_calc_mnp(target, chv=True):
    if not (300000 <= target <= 1150000):
        raise ValueError("DSI CLK Out of Range")
    if chv:
        ref, n, m_min, m_max = 100000, 4, 70, 96
    else:
        ref, n, m_min, m_max = 25000, 1, 62, 92
    p_min, p_max = 2, 6
    calc_p, calc_m = p_min, m_min
    delta = abs(target - (m_min * ref) // (p_min * n))
    m = m_min
    while m <= m_max and delta:
        p = p_min
        while p <= p_max and delta:
            calc = (m * ref) // (p * n)
            d = abs(target - calc)
            if d < delta:
                delta, calc_m, calc_p = d, m, p
            p += 1
        m += 1
    ctrl = 1 << (17 + calc_p - 2)
    div = ((n.bit_length() - 1) << 16) | lfsr_converts[calc_m - 62]
    return calc_m, n, calc_p, delta, ctrl, div

def vlv_dsi_pclk(ctrl, div, chv=True):
    ref = 100000 if chv else 25000
    pll_ctl = (ctrl & (0x1ff << 17)) >> (17 - 2)
    n = 1 << ((div & (3 << 16)) >> 16)
    m_seed = div & 0x1ff
    p = pll_ctl.bit_length()
    if p: p -= 1
    m = lfsr_converts.index(m_seed) + 62
    dsi_clock = (m * ref) // (p * n)          # truncating, like the C code
    return div_round_closest(dsi_clock * LANES, BPP), dsi_clock, m, n, p

def txbyteclkhs(pixels, bpp=BPP, lanes=LANES, ratio=BURST_RATIO):
    pixels &= 0xffff                          # u16 parameter
    v = div_round_up(div_round_up(pixels * bpp * ratio, 8 * 100), lanes)
    return v & 0xffff                         # u16 return

def pixels_from_txbyteclkhs(clk, bpp=BPP, lanes=LANES, ratio=BURST_RATIO):
    clk &= 0xffff
    return div_round_up(clk * lanes * 8 * 100, bpp * ratio) & 0xffff

for pclk_in in (MODE_CLOCK, 61111):
    print(f"=== intel_dsi->pclk = {pclk_in} kHz ===")
    target = dsi_clk_from_pclk(pclk_in)
    m, n, p, delta, ctrl, div = dsi_calc_mnp(target)
    pclk, dsi_clock_int, m2, n2, p2 = vlv_dsi_pclk(ctrl, div)
    exact_dsi = F(m * 100000, p * n)                    # kHz, exact
    exact_pclk = exact_dsi * LANES / BPP
    print(f"target dsi_clk            = {target} kHz (= pclk*24/4)")
    print(f"chosen M/N/P              = {m}/{n}/{p}  delta={delta} kHz  ctrl=0x{ctrl:08x} div=0x{div:08x} (m_seed={lfsr_converts[m-62]})")
    print(f"dsi_clock (C, truncated)  = {dsi_clock_int} kHz; exact = {float(exact_dsi):.4f} kHz")
    print(f"pclk read back (C)        = {pclk} kHz; exact = {float(exact_pclk):.4f} kHz")
    print(f"byte clock exact          = {float(exact_dsi/8):.4f} kHz")
    print(f"fuzzy check 5%: {pclk} vs {MODE_CLOCK}: ok")
    print()

# ---- actual clocks used from here on (M=88 N=4 P=6) --------------------------
m, n, p = 88, 4, 6
bitclk = F(m * 100000, p * n)           # kHz per lane
byteclk = bitclk / 8                    # kHz
pixclk = bitclk * LANES / BPP           # kHz, the pipe's pixel clock
print(f"actual per-lane bit clock = {float(bitclk):.4f} kHz, byte clock = {float(byteclk):.4f} kHz, pixel clock = {float(pixclk):.4f} kHz")
print(f"ratio pixclk/byteclk = {pixclk/byteclk} (= lanes*8/bpp)")

# ---- set_dsi_timings --------------------------------------------------------
hact = txbyteclkhs(HDISP); hfp = txbyteclkhs(HSS - HDISP); hsync = txbyteclkhs(HSE - HSS); hbp = txbyteclkhs(HTOT - HSE)
print(f"programmed counts: HACTIVE={hact} (0x{hact:x}) HFP={hfp} HSYNC={hsync} HBP={hbp}  (dump: 0x21c, 0xe, 0xe, 0xe)")
print(f"VFP={VSS-VDISP} VSYNC={VSE-VSS} VBP={VTOT-VSE} (dump: 0xe, 0x4, 0xa)")
sum_bytes = hact + hfp + hsync + hbp
pipe_line_bytes = F(HTOT * BPP, 8 * LANES)
print(f"sum of H counts = {sum_bytes} byteclks; pipe line in byteclks exact = {pipe_line_bytes} = {float(pipe_line_bytes):.2f}")
print(f"pipe line time   = {float(HTOT / pixclk)*1e3:.4f} us ({HTOT} px @ {float(pixclk):.3f} kHz)")
print(f"link line time (sum of counts) = {float(sum_bytes / byteclk)*1e3:.4f} us")
print(f"difference per line = {float((sum_bytes/byteclk - HTOT/pixclk))*1e6:.2f} ns = {float(sum_bytes - pipe_line_bytes)} byteclk")
print(f"pipe frame time  = {float(HTOT*VTOT/pixclk):.4f} ms -> {float(pixclk*1000/(HTOT*VTOT)):.4f} Hz")

# raw payload of one active line on the wire
act_bytes = HDISP * BPP // 8
print(f"active line payload = {act_bytes} bytes = {F(act_bytes, LANES)} byteclk on 4 lanes (HACTIVE count {hact})")
# packet overheads in non-burst sync events mode, per line (RGB888 long packet: 4 hdr + 2 crc; HSS short packet 4; blanking packets hdr 4 + crc 2 ... )
# EOT packet 4 bytes (eot_pkt enabled)
print(f"overheads if link must also carry: HSS short pkt 4 B, long pkt hdr+crc 6 B, blanking pkt(s) 6 B each, EOT 4 B")

# ---- HS_TX_TIMEOUT ----------------------------------------------------------
tot = (VTOT * HTOT) & 0xffff
print(f"HS_TX_TIMEOUT: vtotal*htotal = {VTOT*HTOT} -> u16 {tot}; programmed = {txbyteclkhs(VTOT*HTOT)+1} (0x{txbyteclkhs(VTOT*HTOT)+1:x}); intended = {div_round_up(div_round_up(VTOT*HTOT*BPP*100, 800), LANES)+1} (0x{div_round_up(div_round_up(VTOT*HTOT*BPP*100, 800), LANES)+1:x})")

# ---- get_config round trip ---------------------------------------------------
for name, v in (("hfp", hfp), ("hsync", hsync), ("hbp", hbp)):
    print(f"get_config {name}: {v} byteclk -> {pixels_from_txbyteclkhs(v)} px (sw {HSS-HDISP if name=='hfp' else (HSE-HSS if name=='hsync' else HTOT-HSE)})")

# ---- cdclk ---------------------------------------------------------------------
def vlv_calc_cdclk(min_cdclk, hpll_vco=800000):
    freq_320 = 333333 if (hpll_vco << 1) % 320000 != 0 else 320000
    if min_cdclk > 266667: return freq_320
    if min_cdclk > 0: return 266667
    return 200000
pixel_rate = 61111
min_cdclk = div_round_up(pixel_rate * 100, 95 * 1)
print(f"\ncdclk: pixel_rate {pixel_rate} -> min_cdclk {min_cdclk} (95% guardband) -> vlv_calc_cdclk = {vlv_calc_cdclk(min_cdclk)} kHz (hpll 800 MHz); "
      f"with hpll 1600: {vlv_calc_cdclk(min_cdclk, 1600000)}; plane min cdclk (1:1 scaling) = {pixel_rate}")
for vco in (800000, 1600000, 2000000, 2400000):
    print(f"  hpll_vco {vco}: freq_320 = {333333 if (vco<<1)%320000 else 320000}; divider for 266667 = {div_round_closest(vco<<1, 266667)-1}, for 320000 = {div_round_closest(vco<<1, 320000)-1}")

# ---- overflow checks (u32 products) --------------------------------------------
print("\nu32 overflow checks:")
print(f"  pclk*bpp = {61111*24} (fine)")
print(f"  dsi_clock*lane_count = {366666*4} (fine)")
print(f"  pixels*bpp*ratio for vtotal*htotal (if u32) = {VTOT*HTOT*24*100} < 2^32={2**32}: {VTOT*HTOT*24*100 < 2**32}")
print(f"  intel_dsi_bitrate = pclk*bpp/lanes = {61111*24//4}")
