#!/usr/bin/env python3
"""Reimplementation of the CHV (vlv_*) watermark/FIFO computation of i915 v7.2.9
(drivers/gpu/drm/i915/display/i9xx_wm.c) for the GPD Win 1 DSI panel.
Integer arithmetic exactly as in C (unsigned int)."""
import sys

USHRT_MAX = 0xffff

def div_round_up(a, b):
    return (a + b - 1) // b

def intel_wm_method2(pixel_rate, htotal, width, cpp, latency):
    # i9xx_wm.c:527  latency in 0.1us, pixel_rate in kHz
    ret = (latency * pixel_rate) // (htotal * 10000)
    ret = (ret + 1) * width * cpp
    return ret

def vlv_wm_method2(pixel_rate, htotal, width, cpp, latency):
    # i9xx_wm.c:1481
    return div_round_up(intel_wm_method2(pixel_rate, htotal, width, cpp, latency), 64)

LAT_US = {0: 3, 1: 12, 2: 33}   # vlv_setup_wm_latency(): PM2, PM5, DDR_DVFS (CHV)
NUM_LEVELS = 3
PLANES = ["PRIMARY", "SPRITE0", "SPRITE1", "CURSOR"]

def compute(pixel_rate, htotal, width, cpp, cursor_visible, sprites=(), npipes=3):
    print(f"pixel_rate={pixel_rate} kHz htotal={htotal} width={width} cpp={cpp} cursor_visible={cursor_visible}")
    linetime_us = htotal * 1000 / pixel_rate
    print(f"line time = {linetime_us:.3f} us, line rate = {pixel_rate/htotal:.2f} kHz, line bytes = {width*cpp}")
    raw = {lvl: {p: 0 for p in PLANES} for lvl in range(NUM_LEVELS)}
    # vlv_raw_plane_wm_compute for each plane
    for p in PLANES:
        visible = (p == "PRIMARY") or (p == "CURSOR" and cursor_visible) or (p in sprites)
        if not visible:
            for lvl in range(NUM_LEVELS):
                raw[lvl][p] = 0
            continue
        lvl = 0
        while lvl < NUM_LEVELS:
            if p == "CURSOR":
                wm = 63
            else:
                wm = vlv_wm_method2(pixel_rate, htotal, width, cpp, LAT_US[lvl] * 10)
                # show the intermediate
                lines = (LAT_US[lvl] * 10 * pixel_rate) // (htotal * 10000)
                print(f"  {p} level {lvl}: latency {LAT_US[lvl]} us -> floor(lat/linetime)={lines} -> ({lines}+1)*{width*cpp} B = {(lines+1)*width*cpp} B -> {wm} cachelines")
            wm = min(wm, USHRT_MAX)
            max_wm = 63 if p == "CURSOR" else 511
            if wm > max_wm:
                break
            raw[lvl][p] = wm
            lvl += 1
        for l2 in range(lvl, NUM_LEVELS):
            raw[l2][p] = USHRT_MAX
    print("raw watermarks (cachelines needed):")
    for lvl in range(NUM_LEVELS):
        print(f"  level {lvl}: " + " ".join(f"{p}={raw[lvl][p]}" for p in PLANES))

    # vlv_compute_fifo (uses level PM2 raw)
    active = ["PRIMARY"] + list(sprites)
    fifo_size = 511
    r = raw[0]
    sprite0_extra = 1 if ("SPRITE1" in sprites and "SPRITE0" not in sprites) else 0
    total_rate = r["PRIMARY"] + r["SPRITE0"] + r["SPRITE1"] + sprite0_extra
    assert total_rate <= fifo_size
    total_rate = total_rate or 1
    fifo = {}
    fifo_left = fifo_size
    for p in ["PRIMARY", "SPRITE0", "SPRITE1"]:
        if p not in active:
            fifo[p] = 0
            continue
        fifo[p] = fifo_size * r[p] // total_rate
        fifo_left -= fifo[p]
    fifo["SPRITE0"] = fifo.get("SPRITE0", 0) + sprite0_extra
    fifo_left -= sprite0_extra
    fifo["CURSOR"] = 63
    fifo_extra = div_round_up(fifo_left, len(active) or 1)
    for p in ["PRIMARY", "SPRITE0", "SPRITE1"]:
        if fifo_left == 0:
            break
        if p not in active:
            continue
        extra = min(fifo_extra, fifo_left)
        fifo[p] += extra
        fifo_left -= extra
    print(f"FIFO split (cachelines of 64 B): {fifo}  -> DSPARB sprite0_start={fifo['PRIMARY']} sprite1_start={fifo['PRIMARY']+fifo['SPRITE0']}")

    # _vlv_compute_pipe_wm
    sr_fifo_size = npipes * 512 - 1
    def invert(wm, size):
        return USHRT_MAX if wm > size else size - wm
    num_levels = NUM_LEVELS
    wm_state = {}
    for lvl in range(NUM_LEVELS):
        valid = all(raw[lvl][p] <= fifo[p] for p in PLANES)
        if not valid:
            num_levels = lvl
            break
        wm_state[lvl] = {p: invert(raw[lvl][p], fifo[p]) for p in PLANES}
        wm_state[lvl]["SR"] = invert(max(raw[lvl]["PRIMARY"], raw[lvl]["SPRITE0"], raw[lvl]["SPRITE1"]), sr_fifo_size)
        wm_state[lvl]["SR_CURSOR"] = invert(raw[lvl]["CURSOR"], 63)
    cxsr = len(active) == 1   # pipe B != PIPE_C
    print(f"usable levels: {num_levels} (0=PM2,1=PM5,2=DDR_DVFS); cxsr(maxfifo)={cxsr}")
    for lvl in range(num_levels):
        print(f"  level {lvl} programmed (inverted) : " + " ".join(f"{k}={v}" for k, v in wm_state[lvl].items()))
    # vlv_merge_wm with a single active pipe: level = num_levels-1
    lvl = num_levels - 1
    w = wm_state[lvl]
    print(f"merged: level={lvl} cxsr={cxsr}")
    # registers
    def FW(v, shift, mask):
        return (v << shift) & (mask << shift)
    dspfw1 = FW(w["SR"] if cxsr else 0, 23, 0x1ff) | FW(w["CURSOR"], 16, 0x3f) | FW(w["PRIMARY"], 8, 0xff)
    dspfw3 = FW(w["SR_CURSOR"] if cxsr else 0, 24, 0x3f)
    dspfw7 = FW(w["SPRITE1"], 16, 0xff) | FW(w["SPRITE0"], 0, 0xff)
    dsphowm = FW((w["SR"] if cxsr else 0) >> 9, 24, 3) | FW(w["SPRITE1"] >> 8, 20, 1) | FW(w["SPRITE0"] >> 8, 16, 1) | FW(w["PRIMARY"] >> 8, 12, 1)
    ddl = (0x82 << 24) | (0x82 << 16) | (0x82 << 8) | 0x82
    print(f"  DSPFW1 (0x1f0034) = 0x{dspfw1:08x}  [SR={w['SR'] if cxsr else 0} CURSORB={w['CURSOR']} PLANEB={w['PRIMARY']} PLANEA=0]")
    print(f"  DSPFW3 (0x1f003c) = 0x{dspfw3:08x}  [CURSOR_SR={w['SR_CURSOR'] if cxsr else 0}]")
    print(f"  DSPFW7_CHV (0x1f00b4) = 0x{dspfw7:08x}  [SPRITED={w['SPRITE1']} SPRITEC={w['SPRITE0']}]")
    print(f"  DSPHOWM (0x1f0064) = 0x{dsphowm:08x}")
    print(f"  VLV_DDL(B) (0x1f0054) = 0x{ddl:08x}  (DDL_PRECISION_HIGH|2 for all planes, hardcoded in vlv_merge_wm)")
    print(f"  FW_BLC_SELF_VLV (0x186500) bit15 FW_CSPWRDWNEN = {int(cxsr)}; Punit DSPSSPM PM5 = {int(lvl>=1)}; DDR DVFS allowed (FORCE_DDR_HIGH_FREQ cleared) = {int(lvl>=2)}")
    # time budget
    for lvl2 in range(num_levels):
        need = raw[lvl2]["PRIMARY"] * 64
        print(f"  level {lvl2}: FIFO data required before fetch = {need} B = {need/(width*cpp):.2f} lines = {need/(width*cpp)*linetime_us:.2f} us of active scan-out (latency {LAT_US[lvl2]} us)")
    print()

if __name__ == "__main__":
    compute(61000, 774, 720, 4, cursor_visible=False)
    compute(61000, 774, 720, 4, cursor_visible=True)
    compute(61111, 774, 720, 4, cursor_visible=False)
    # sanity: landscape 1280x720 would give
    compute(61000, 1308 if False else 1650, 1280, 4, cursor_visible=False)
