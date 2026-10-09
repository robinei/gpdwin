from math import ceil
def rup(a,b): return -(-a//b)
# mode
hd,hss,hse,ht = 720,738,756,774
vd,vss,vse,vt = 1280,1294,1298,1308
bpp,lanes,bmr = 24,4,100
pclk_vbt = 61000
# PLL (CHV)
dsi_clk_t = round(pclk_vbt*bpp/lanes)   # DIV_ROUND_CLOSEST
best=None
for m in range(70,97):
    for p in range(2,7):
        c=(m*100000)//(p*4); d=abs(dsi_clk_t-c)
        if best is None or d<best[0]: best=(d,m,p,c)
print("PLL target",dsi_clk_t,"best m,p,clk",best)
dsi_clk=best[3]; pclk=round(dsi_clk*lanes/bpp)
print("pclk readback",pclk)
pclk_used = pclk  # GOP readback passes fuzzy check -> intel_dsi->pclk = 61111
bitrate = pclk_used*bpp//lanes
print("intel_dsi->pclk",pclk_used,"bitrate kbps",bitrate)
ui_num=1000000; ui_den=bitrate; mul=2
tlpx=50
lp_byte_clk=rup(tlpx*ui_den,8*ui_num)
ths_prepare=max(50,50); prepare=rup(ths_prepare*ui_den,ui_num*mul)
exit_zero=rup((200-ths_prepare)*ui_den,ui_num*mul)
if exit_zero < (55*ui_den//ui_num) and (55*ui_den)%ui_num: exit_zero+=1
clk_zero=rup((310-ths_prepare)*ui_den,ui_num*mul)
trail=rup(max(70,60)*ui_den,ui_num*mul)
dphy=exit_zero<<24|trail<<16|clk_zero<<8|prepare
print("lp_byte_clk",lp_byte_clk,"prepare",prepare,"exit_zero",exit_zero,"clk_zero",clk_zero,"trail",trail,"dphy_reg 0x%08x"%dphy)
tlpx_ui=rup(tlpx*ui_den,ui_num)
lp2hs=rup(4*tlpx_ui+prepare*mul+exit_zero*mul+10,8)
hs2lp=rup(60+2*tlpx_ui,8)
hs2lp_ui=rup(rup(60*ui_den,ui_num)+2*tlpx_ui,8)
extra=3
print("tlpx_ui",tlpx_ui,"lp2hs",lp2hs,"hs2lp(code, ns as UI)",hs2lp,"hs2lp(if ns->UI)",hs2lp_ui,"HIGH_LOW_SWITCH",max(lp2hs,hs2lp)+extra)
clk_lp2hs=rup(4*tlpx_ui+prepare*2+clk_zero*2,8)+extra
clk_hs2lp=rup(2*tlpx_ui+trail*2+8,8)+extra
print("CLK_LANE_SWITCH 0x%08x"%(clk_lp2hs<<16|clk_hs2lp))
def tx(px): return rup(rup(px*bpp*bmr,8*100),lanes)
def tx16(px): return tx(px&0xffff)&0xffff
hfp,hs,hbp=hss-hd,hse-hss,ht-hse
print("hactive",tx(hd),"hfp",tx(hfp),"hsync",tx(hs),"hbp",tx(hbp),"vfp",vss-vd,"vsync",vse-vss,"vbp",vt-vse)
print("HS_TX_TIMEOUT u16 path: 0x%x  (pixels arg %d)"%(tx16(vt*ht)+1, (vt*ht)&0xffff), " correct: 0x%x"%(tx(vt*ht)+1), " ret-only-u16 would be 0x%x"%((tx(vt*ht)&0xffff)+1))
# line budget
byteclk_khz=bitrate/8
line_bc=ht*bpp/(8*lanes)
print("byteclk MHz",byteclk_khz/1000,"pixel line in byteclk",line_bc,"us",ht/pclk*1000)
print("programmed: active+hfp+hbp =",tx(hd)+tx(hfp)+tx(hbp)," +hsync =",tx(hd)+tx(hfp)+tx(hs)+tx(hbp))
print("rounding: per segment", tx(hfp)-hfp*bpp/(8*lanes))
print("frame byteclk exact",line_bc*vt,"  HS_TX correct 0x%x = %d"%(tx(vt*ht)+1,tx(vt*ht)+1))
print("0x55ff =",0x55ff,"byteclk =",0x55ff/byteclk_khz*1000,"us =",0x55ff/line_bc,"lines")
print("0x3fffff =",0x3fffff/byteclk_khz/1000,"ms =",0x3fffff/(line_bc*vt),"frames")
print("refresh",pclk*1000/(ht*vt), pclk_vbt*1000/(ht*vt))
print("init_count txclkesc(20MHz,100us)=",20*100)
# medfield floor variant
print("medfield floor counts:", (hfp*bpp)//(lanes*8),(hd*bpp)//(lanes*8))
# readback (bxt path only) pixels_from
def pf(c): return rup(c*lanes*8*100, bpp*bmr)
print("pixels_from(14)=",pf(14),"(mode had 18)")
