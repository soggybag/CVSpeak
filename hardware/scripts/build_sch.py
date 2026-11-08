"""CVSpeak schematic, dev board v0 (draft, 2026-11-07). Plain python3, from the hardware folder:
   python3 scripts/make_symbols.py && python3 scripts/build_sch.py      -> CVSpeak.kicad_sch
One A1 sheet; blocks are joined by net labels. Pin map: docs/Pin map.md. Decisions: docs/Hardware Decisions.md.
The helpers (sch.py, sx.py, lib.py) come from Palimpsest, and so do the power, CV, gate and output blocks.

References follow the panel (tools/make_panel.py):
  J1 TRIG, J2 RESET, J3 V/OCT, J4 SEL, J5 SPEED CV, J6 FORM CV, J7 BEND CV, J8 CARRIER, J9 EOW, J10 OUT, J11 power
  RV1-6 PITCH SPEED FORMANT BEND BLEND INTONE; SW1 BACK, SW2 SPEAK, SW3 encoder; D_LED SPEAKING; DS1 OLED
  U1 Pico 2, U2 ES8388, U3-U4 TL074, U5 OPA1678, U6-U7 74HC4051, U8 buck, U9 3V3A LDO

Blocks
  POWER    Eurorack 2x5 (J11), 1N5819WS per rail, 10u + 100n per rail. AP63205 buck 12 V -> 5 V. 5 V -> Schottky -> Pico
           VSYS (USB can be plugged in at the same time). LP5907 LDO 5 V -> 3V3A (low noise): codec AVDD / HPVDD, pots,
           muxes, CV bias, Pico ADC_VREF.
  PICO     U1 on 2x20 female headers. Every GPIO labelled with its pin-map net.
  CODEC    ES8388, I2C 0x10 (CE low), USB mode 12 MHz MCLK (GP21 GPOUT0, 33R series), codec is the I2S master.
           DVDD / PVDD on 3V3, AVDD / HPVDD on 3V3A. VREF / ADCVREF / VMID: 10u + 100n each.
  AUDIO    CARRIER: x-0.2 (100k / 20k) -> 10u -> LIN1. 10 Vpp -> 2 Vpp (codec full scale 2.83 Vpp).
           OUT: LOUT1 -> 10u -> 20k -> Rf 68k (x-3.4) -> 1k. Full scale 2.83 Vpp -> 9.6 Vpp.
           Carrier detect: J8 TN pulled up to 3V3A through 1M, filtered by 100n, read on mux B X5.
           Unpatched, TN touches the tip, which sits at 0 V through 100k: TN reads ~0.3 V. Patched: 3.3 V.
  CV IN    SEL, SPEED, FORM, BEND: Palimpsest v0.2 stage, Vout = 1.16 x CV_BIAS - 0.16 x Vin, +-10 V -> 3.24..0.04 V.
           V/OCT: 0.1 % (100k in, 33k feedback, x-0.33), VOCT_BIAS = 3V3A x 12k / 32k = 1.24 V, so 0 V -> 1.65 V,
           +-5 V -> 3.3..0 V. Own ADC pin (GP26). 1k + BAT54S clamp on every stage.
  GATES    TRIG, RESET: MMBT3904 inverter, 100k series / 100k to ground (threshold ~1.2 V), 1N4148W clamp, 10k pull-up.
  EOW      GP10 -> non-inverting x2.8 (18k / 10k) -> 0 / 9.2 V -> 1k. 100k pull-down: 0 V while the Pico boots.
  UI       Two 74HC4051 on 3V3A share MUX_S0-2. A: 6 pots, BACK, encoder push. B: 4 CVs, SPEAK, Carrier detect,
           2 spares (100k to ground, test pads). Buttons: 10k pull-up to 3V3A, press = 0 V.
           Encoder A / B: 10k pull-ups + 10n (debounce), direct GPIO. OLED SH1106 SPI, CS tied low. LED 1k.
"""
import sys
sys.path.insert(0, 'scripts')
from sch import *
from lib import pins as libpins

OUT = sys.argv[1] if len(sys.argv) > 1 else 'CVSpeak.kicad_sch'
g = lambda n: round(n * 2.54, 3)
s = Sheet('CVSpeak', 'A1')
W = s.wire
P = s.P

FR = 'Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder'
FC = 'Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder'
FJACK = 'Connector_Audio:Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles'
FPOT = 'Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical'
FBTN = 'CVSpeak:SW_Push_Round_D12mm_P5.00mm_DAIER_TSL-A064'
# LCSC numbers: from Palimpsest (2026-11-04) unless marked NEW. NEW ones are to check on lcsc.com before the order.
LR = {'1k': 'C17513', '4k7': 'C17673', '10k': 'C17414', '12k': 'C17444', '16k': 'C17490', '18k': 'C17506',
      '20k': 'C4328', '100k': 'C149504',
      '100k 0.1%': 'C122537',    # YAGEO RT0805BRD07100KL, thin film 25 ppm
      '33k 0.1%': 'C2849182',    # RESI PTFR0805B33K0P9, thin film 25 ppm
      '68k': 'C17801',           # NEW
      '1M': 'C17514',            # NEW
      '33R': 'C17634'}           # NEW
LC = {'100n': 'C49678', '1u': 'C28323', '10u': 'C15850', '22u': 'C45783', '10n': 'C1710'}  # 10n NEW

_n = {}
def nx(p):
    _n[p] = _n.get(p, 0) + 1
    return f'{p}{_n[p]}'
def pw(kind, p, d=None): s.power(kind, p, d)
def lab(name, p, side='r'): s.label(name, p, side)
def stub(p, name, dx):
    q = (round(p[0] + dx, 3), p[1]); W(p, q); lab(name, q, 'r' if dx > 0 else 'l'); return q
def R(v, x, y, rot, text, d=''):
    r = nx('R'); s.part(r, 'Device', 'R', v, x, y, rot, fp=FR, lcsc=LR.get(v), desc=d, text=text); return r
def Rh(v, cx, y, text='t', d=''): return R(v, cx, y, 90, text, d)
def Rv(v, x, cy, text='r', d=''): return R(v, x, cy, 0, text, d)
def C(v, x, y, rot, text, d=''):
    c = nx('C'); s.part(c, 'Device', 'C', v, x, y, rot, fp=FC, lcsc=LC.get(v), desc=d, text=text); return c
def Ch(v, cx, y, text='t', d=''): return C(v, cx, y, 90, text, d)
def Cv(v, x, cy, text='r', d=''): return C(v, x, cy, 0, text, d)
def jack(ref, label, x, y, rot=0, mirror=None, text=None):
    s.part(ref, 'Connector_Audio', 'AudioJack2_SwitchT', label, x, y, rot, mirror=mirror, fp=FJACK,
           desc=f'Thonkiconn PJ398SM, panel: {label}', text=text or ((x, y - 8.89), (x, y - 6.35), None),
           extra={'Panel': label})
def gnd_right(p): W(p, (p[0] + 2.54, p[1])); pw('GND', (p[0] + 2.54, p[1]))
def gnd_left(p): W(p, (p[0] - 2.54, p[1])); pw('GND', (p[0] - 2.54, p[1]))
OPS = {'TL074': ('Package_SO:SOIC-14_3.9x8.7mm_P1.27mm', 'Quad JFET op-amp', 'C6963'),
       'OPA1678': ('Package_SO:SOIC-8_3.9x4.9mm_P1.27mm', 'Dual low-noise audio op-amp', 'C192421')}
UNITPINS = {'TL074': {1: ('3', '2', '1'), 2: ('5', '6', '7'), 3: ('10', '9', '8'), 4: ('12', '13', '14')},
            'OPA1678': {1: ('3', '2', '1'), 2: ('5', '6', '7')}}
def OP(ref, sym, unit, x, y):
    fp, d, lc = OPS[sym]
    s.part(ref, 'Amplifier_Operational', sym, sym, x, y, 0, unit=unit, fp=fp, lcsc=lc, desc=d, text='ic')
    pl, mi, ou = UNITPINS[sym][unit]
    return P(ref, pl), P(ref, mi), P(ref, ou)

# IC pins: a stub in the direction the pin faces (parts placed at rot 0, no mirror).
ANG = {}
def ic(ref, lib, name, value, x, y, **kw):
    s.part(ref, lib, name, value, x, y, 0, **kw)
    for num, lst in libpins(lib, name).items(): ANG[(ref, num)] = lst[0][3]
OUTWARD = {0: (-1, 0, 'left'), 180: (1, 0, 'right'), 90: (0, 1, 'down'), 270: (0, -1, 'up')}
POWER = ('+12V', '-12V', '+5V', '+3V3', '+3.3VA', 'GND')
def term(ref, num, net, length=5.08):
    p = P(ref, num); dx, dy, d = OUTWARD[int(ANG[(ref, num)])]
    if net is None: s.noconnect(p); return
    if net in POWER:
        q = (round(p[0] + dx * 2.54, 3), round(p[1] + dy * 2.54, 3)); W(p, q); pw(net, q, d); return q
    q = (round(p[0] + dx * length, 3), round(p[1] + dy * length, 3)); W(p, q)
    lab(net, q, 'l' if dx < 0 else 'r')
    return q

TITLE = {'title': 'CVSpeak - speech synthesiser, Pico 2 + ES8388 (14 HP)', 'date': '2026-11-07', 'rev': '0.0',
         'company': 'Super-Freq.com',
         'comments': ['Draft schematic: not checked on the bench; LCSC numbers marked NEW to verify',
                      'Pin map: docs/Pin map.md; decisions: docs/Hardware Decisions.md',
                      'Generated by scripts/build_sch.py']}

# ---------------------------------------------------------------- POWER
s.box('POWER: Eurorack 2x5, reverse protection, 12 V -> 5 V buck, 5 V -> VSYS, 3V3A LDO', g(4), g(8), g(90), g(40))
J = (g(14), g(16))
s.part('J11', 'Connector_Generic', 'Conn_02x05_Odd_Even', 'EURO_PWR_2x5', J[0], J[1], 0,
       fp='Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical', desc='Eurorack power, red stripe = pin 1 = -12 V',
       text=((J[0] + 1.27, J[1] - 10.16), (J[0] + 1.27, J[1] - 7.62), None))
xl, xr = J[0] - 7.62, J[0] + 10.16
for n in ('3', '5', '7'): W(P('J11', n), (xl, P('J11', n)[1]))
for n in ('4', '6', '8'): W(P('J11', n), (xr, P('J11', n)[1]))
W((xl, P('J11', '3')[1]), (xl, P('J11', '7')[1])); W((xr, P('J11', '4')[1]), (xr, P('J11', '8')[1]))
W((xl, J[1]), (xl - 2.54, J[1])); pw('GND', (xl - 2.54, J[1]), 'left')
W((xr, J[1]), (xr + 2.54, J[1])); pw('GND', (xr + 2.54, J[1]), 'right')
yn, yp = J[1] - 7.62, J[1] + 7.62
W(P('J11', '1'), (xl, P('J11', '1')[1]), (xl, yn)); W(P('J11', '2'), (xr, P('J11', '2')[1]), (xr, yn)); W((xl, yn), (xr, yn))
W(P('J11', '9'), (xl, P('J11', '9')[1]), (xl, yp)); W(P('J11', '10'), (xr, P('J11', '10')[1]), (xr, yp)); W((xl, yp), (xr, yp))
dx = g(22)
s.part('D1', 'Diode', '1N5819WS', '1N5819WS', dx, yn, 0, fp='Diode_SMD:D_SOD-323', lcsc='C191023', desc='-12 V reverse protection', text='t')
s.part('D2', 'Diode', '1N5819WS', '1N5819WS', dx, yp, 180, fp='Diode_SMD:D_SOD-323', lcsc='C191023', desc='+12 V reverse protection', text='b')
_n['D'] = 2
W((xr, yn), P('D1', '1')); W((xr, yp), P('D2', '2'))
x1, x2, xe = g(25), g(30), g(34)
W(P('D1', '2'), (xe, yn)); W(P('D2', '1'), (xe, yp))
for x, v in ((x1, '10u'), (x2, '100n')):
    Cv(v, x, yn + 3.81, 'l'); Cv(v, x, yp - 3.81, 'l')
    pw('GND', (x, J[1]), 'right')
pw('-12V', (xe, yn)); pw('+12V', (xe, yp))
pw('PWR_FLAG', (xe, yn), 'down'); pw('PWR_FLAG', (xe, yp), 'down'); pw('PWR_FLAG', (xl - 2.54, J[1]))  # GND
# buck
B = (g(52), g(16))
s.part('U8', 'Regulator_Switching', 'AP63205WU', 'AP63205WU', B[0], B[1], 0, fp='Package_TO_SOT_SMD:TSOT-23-6', lcsc='C2071056',
       desc='5 V 2 A sync buck, 1.1 MHz', text=((B[0], B[1] - 7.62), (B[0], B[1] - 5.33), None))
xin = B[0] - 12.7
W(P('U8', '3'), (xin, P('U8', '3')[1])); W(P('U8', '2'), (xin, P('U8', '2')[1])); W((xin, P('U8', '3')[1]), (xin, P('U8', '2')[1]))
W((xin, P('U8', '3')[1]), (xin, P('U8', '3')[1] - 2.54)); pw('+12V', (xin, P('U8', '3')[1] - 2.54))
xc = xin - 5.08
W((xin, P('U8', '3')[1]), (xc, P('U8', '3')[1])); Cv('10u', xc, P('U8', '3')[1] + 3.81, 'l', d='buck input, 25 V')
pw('GND', (xc, P('U8', '3')[1] + 7.62))
pw('GND', P('U8', '4'))
pw('+5V', P('U8', '1'), 'right')
ysw = P('U8', '5')[1]
xb = B[0] + 17.78
W(P('U8', '6'), (xb, P('U8', '6')[1]), (xb, B[1] + 7.62))
Ch('100n', xb + 3.81, B[1] + 7.62, 'b', d='bootstrap')
W((xb + 7.62, B[1] + 7.62), (xb + 7.62, ysw))
s.part('L1', 'Device', 'L', '4.7u', xb + 15.24, ysw, 90, fp='Inductor_SMD:L_Bourns-SRN4018', lcsc='C780206',
       desc='buck inductor, Bourns SRN4018-4R7M 1.9 A', text='t')
W(P('U8', '5'), P('L1', '1'))
xo = xb + 22.86
W(P('L1', '2'), (xo + 5.08, ysw))
pw('+5V', (xo, ysw))
W((xo + 5.08, ysw), (xo + 10.16, ysw)); pw('PWR_FLAG', (xo + 10.16, ysw))
Cv('22u', xo, ysw + 3.81, 'l', d='buck output'); Cv('22u', xo + 5.08, ysw + 3.81, 'r', d='buck output')
pw('GND', (xo, ysw + 7.62)); pw('GND', (xo + 5.08, ysw + 7.62))
# 5 V -> VSYS
VY = g(30)
pw('+5V', (g(10), VY))
d = nx('D')
s.part(d, 'Diode', '1N5819WS', '1N5819WS', g(14), VY, 180, fp='Diode_SMD:D_SOD-323', lcsc='C191023',
       desc='5 V -> Pico VSYS (USB can be plugged in too)', text='t')
W((g(10), VY), P(d, '2')); W(P(d, '1'), (g(20), VY)); lab('VSYS', (g(20), VY)); pw('PWR_FLAG', (g(18), VY))
# 3V3A LDO
LX, LY = g(44), g(32)
s.part('U9', 'Regulator_Linear', 'LP5907MFX-3.3', 'LP5907MFX-3.3', LX, LY, 0, fp='Package_TO_SOT_SMD:SOT-23-5',
       desc='3V3A low-noise LDO, 250 mA, 6.5 uVrms (LCSC: check stock)', text=((LX, LY - 8.89), (LX, LY - 6.35), None))
pin_in, pin_en = P('U9', '1'), P('U9', '3')
xi = pin_in[0] - 5.08
W(pin_in, (xi, pin_in[1])); W(pin_en, (xi, pin_en[1])); W((xi, pin_in[1]), (xi, pin_en[1]))
W((xi, pin_in[1]), (xi, pin_in[1] - 2.54)); pw('+5V', (xi, pin_in[1] - 2.54))
W((xi, pin_in[1]), (xi - 5.08, pin_in[1])); Cv('1u', xi - 5.08, pin_in[1] + 3.81, 'l', d='LDO input'); pw('GND', (xi - 5.08, pin_in[1] + 7.62))
pw('GND', P('U9', '2')); s.noconnect(P('U9', '4'))
po = P('U9', '5')
W(po, (po[0] + 17.78, po[1])); pw('+3.3VA', (po[0] + 5.08, po[1]))
Cv('1u', po[0] + 10.16, po[1] + 3.81, 'r', d='LDO output (LP5907: 1u ceramic)'); pw('GND', (po[0] + 10.16, po[1] + 7.62))
Cv('10u', po[0] + 17.78, po[1] + 3.81, 'r', d='3V3A bulk'); pw('GND', (po[0] + 17.78, po[1] + 7.62))
s.text('Op-amps on +-12 V after the diodes. 3V3 comes from the Pico (its own buck-boost);\n'
       '3V3A (U9) is for the codec analog supplies, pots, muxes, CV bias and the Pico ADC_VREF.', g(6), g(39))

# ---------------------------------------------------------------- PICO 2
PX, PY = g(118), g(30)
s.box('PICO 2 U1 (2x20 female headers, back of the board)', g(94), g(8), g(142), g(56))
ic('U1', 'CVSpeak', 'Pico2', 'Pico 2', PX, PY, fp='Module:RaspberryPi_Pico_Common_THT',
   desc='Raspberry Pi Pico 2 (RP2350) on 2x20 female headers', text=((PX - 7.62, PY - 40.64), (PX + 7.62, PY - 40.64), None), bom=False)
PICO = {1: 'I2C_SDA', 2: 'I2C_SCL', 4: 'OLED_SCK', 5: 'OLED_MOSI', 6: 'OLED_DC', 7: 'OLED_RES', 9: 'ENC_A', 10: 'ENC_B',
        11: 'GATE_TRIG', 12: 'GATE_RESET', 14: 'EOW_GPIO', 15: 'LED_SPEAK', 16: 'MUX_S0', 17: 'MUX_S1', 19: 'MUX_S2',
        20: 'SPARE_15', 21: 'I2S_DOUT', 22: 'I2S_DIN', 24: 'I2S_BCLK', 25: 'I2S_LRCK', 26: 'SPARE_20', 27: 'MCLK_GP21',
        29: 'SPARE_22', 31: 'ADC_VOCT', 32: 'ADC_MUX_A', 34: 'ADC_MUX_B', 35: '+3.3VA', 36: '+3V3', 33: 'GND', 3: 'GND',
        39: 'VSYS', 30: 'RUN', 37: None, 40: None}
for n, net in PICO.items(): term('U1', str(n), net)
s.text('ADC_VREF from 3V3A (the module has 200R from its 3V3: harmless).\n'
       'RUN to a test pad (reset). 3V3_EN and VBUS unused.\n'
       'GP19 must be GP18 + 1, GP17 = GP18 - 1 (PIO I2S).', g(96), g(52))

# ---------------------------------------------------------------- CODEC
CX, CY = g(178), g(30)
s.box('CODEC U2: ES8388, I2C 0x10, master, 48 kHz from 12 MHz MCLK', g(146), g(8), g(212), g(56))
ic('U2', 'Audio', 'ES8388', 'ES8388', CX, CY, fp='Package_DFN_QFN:QFN-28-1EP_4x4mm_P0.45mm_EP2.6x2.6mm', lcsc='C365736',
   desc='Everest ES8388 stereo codec', text=((CX + 5.08, CY - 25.4), (CX + 5.08, CY + 25.4), None))
CODEC = {'1': 'CODEC_MCLK', '5': 'I2S_BCLK', '6': 'I2S_DOUT', '7': 'I2S_LRCK', '8': 'I2S_DIN', '26': 'GND',
         '27': 'I2C_SDA', '28': 'I2C_SCL', '9': None, '25': None, '11': None, '14': None, '15': None,
         '21': None, '22': None, '23': None, '12': 'CODEC_OUT_L', '24': 'CODEC_IN_L',
}
for n, net in CODEC.items(): term('U2', n, net)
# reference caps: VREF, ADCVREF, VMID (pin 10 is the top one, so its wire runs furthest left, above the others)
for k, n in enumerate(('20', '19', '10')):
    p = P('U2', n); xa = p[0] - 7.62 - k * 17.78
    W(p, (xa - 5.08, p[1]))
    Cv('10u', xa, p[1] + 3.81, 'r', d='codec reference decoupling'); pw('GND', (xa, p[1] + 7.62))
    Cv('100n', xa - 5.08, p[1] + 3.81, 'l', d='codec reference decoupling'); pw('GND', (xa - 5.08, p[1] + 7.62))
s.text('VMID / ADCVREF / VREF', P('U2', '10')[0] - 45.72, P('U2', '20')[1] + 13.97)
# supplies, top pins: DVDD 2 + PVDD 3 on 3V3 (left), AVDD 17 + HPVDD 16 on 3V3A (right)
for a, b_, rail, dx_ in (('2', '3', '+3V3', -5.08), ('16', '17', '+3.3VA', 5.08)):
    pa, pb = P('U2', a), P('U2', b_)
    y_ = pa[1] - 2.54
    W(pa, (pa[0], y_)); W(pb, (pb[0], y_)); W((pa[0], y_), (pb[0], y_))
    end = pa if pa[0] * dx_ > pb[0] * dx_ else pb
    W((end[0], y_), (end[0] + dx_, y_)); pw(rail, (end[0] + dx_, y_))
# grounds, bottom pins: DGND 4, AGND 18, HPGND 13, EPAD 29 tied under the chip
gp = [P('U2', n) for n in ('4', '18', '13', '29')]
yg = gp[0][1] + 2.54
for p in gp: W(p, (p[0], yg))
W((min(p[0] for p in gp), yg), (max(p[0] for p in gp), yg)); pw('GND', (P('U2', '13')[0], yg))
s.text('Codec supplies: 10u + 100n on each of DVDD, PVDD, AVDD, HPVDD (decoupling block, right).', g(148), g(54))
# MCLK series resistor
rm = Rh('33R', g(192), g(12), 't', d='MCLK series (source termination, at the Pico)')
lab('MCLK_GP21', (P(rm, '1')[0] - 2.54, g(12)), 'l'); W((P(rm, '1')[0] - 2.54, g(12)), P(rm, '1'))
W(P(rm, '2'), (P(rm, '2')[0] + 2.54, g(12))); lab('CODEC_MCLK', (P(rm, '2')[0] + 2.54, g(12)))
# I2C pull-ups
for i, net in enumerate(('I2C_SDA', 'I2C_SCL')):
    x = g(200) + i * 7.62
    Rv('4k7', x, g(19.5), 'l' if i == 0 else 'r', d='I2C pull-up'); pw('+3V3', (x, g(18)))
    W((x, g(21)), (x, g(22))); lab(net, (x, g(22)), 'l' if i == 0 else 'r')
# codec supply decoupling: one 10u + one 100n per supply pin, placed at the pins on the board
for row, y in enumerate((g(30), g(40))):
    for k, rail in enumerate(('+3V3', '+3V3', '+3.3VA', '+3.3VA')):
        x = g(198) + k * g(3)
        pw(rail, (x, y)); Cv('10u' if row == 0 else '100n', x, y + 3.81, 'r', d='codec supply decoupling'); pw('GND', (x, y + 7.62))
s.text('DVDD PVDD AVDD HPVDD:\none 10u + one 100n per pin', g(197), g(50))

# ---------------------------------------------------------------- AUDIO
s.box('AUDIO: CARRIER x-0.2 -> LIN1; LOUT1 -> x-3.4 -> OUT (OPA1678); carrier detect', g(216), g(8), g(300), g(56))
X, Y = g(250), g(24)
pl, mi, ou = OP('U5', 'OPA1678', 1, X, Y)
jack('J8', 'CARRIER', X - 35.56, Y + 2.54, mirror='x')
rin = Rh('100k', X - 17.78, Y + 2.54, 'b', d='carrier input, 100k')
W(P('J8', 'T'), P(rin, '1')); W(P(rin, '2'), mi)
p = P('J8', 'S'); gnd_right(p)
W(pl, (X - 12.7, pl[1])); pw('GND', (X - 12.7, pl[1]), 'left')
rf = Rh('20k', X, Y + 7.62, 'b', d='carrier gain -0.2: 10 Vpp -> 2 Vpp (full scale 2.83 Vpp)')
W((X - 10.16, mi[1]), (X - 10.16, Y + 7.62), P(rf, '1')); W(P(rf, '2'), (X + 10.16, Y + 7.62), (X + 10.16, Y))
c = Ch('10u', X + 15.24, Y, 't', d='DC block to codec (codec side at VMID)'); W(ou, P(c, '1')); W(P(c, '2'), (X + 22.86, Y)); lab('CODEC_IN_L', (X + 22.86, Y))
# carrier detect on TN
tn = P('J8', 'TN'); xd = tn[0] + 5.08
yd = tn[1] - 10.16
W(tn, (xd, tn[1]), (xd, yd))
rd = Rv('1M', xd, yd - 3.81, 'l', d='carrier detect pull-up'); pw('+3.3VA', P(rd, '1'))
W((xd, yd), (xd + 7.62, yd)); Cv('100n', xd + 7.62, yd - 3.81, 'r', d='carrier detect filter'); pw('GND', (xd + 7.62, yd - 7.62), 'up')
W((xd + 7.62, yd), (xd + 12.7, yd)); lab('CARRIER_DET', (xd + 12.7, yd))
s.text('CARRIER_DET: unpatched ~0.3 V (TN on the tip,\n100k to virtual ground); patched 3.3 V.', g(218), g(33))
# output
X, Y = g(250), g(44)
pl, mi, ou = OP('U5', 'OPA1678', 2, X, Y)
lab('CODEC_OUT_L', (X - 35.56, Y + 2.54), 'l')
c = Ch('10u', X - 27.94, Y + 2.54, 't', d='DC block from codec (VMID)')
rin = Rh('20k', X - 17.78, Y + 2.54, 'b', d='audio out gain')
W((X - 35.56, Y + 2.54), P(c, '1')); W(P(c, '2'), P(rin, '1')); W(P(rin, '2'), mi)
W(pl, (X - 12.7, pl[1])); pw('GND', (X - 12.7, pl[1]), 'left')
rf = Rh('68k', X, Y + 7.62, 'b', d='audio out gain -3.4: 2.83 Vpp -> 9.6 Vpp')
W((X - 10.16, mi[1]), (X - 10.16, Y + 7.62), P(rf, '1')); W(P(rf, '2'), (X + 10.16, Y + 7.62), (X + 10.16, Y))
ro = Rh('1k', X + 15.24, Y, 't', d='output series R'); W(ou, P(ro, '1'))
jack('J10', 'OUT', X + 27.94, Y, rot=180, text=((X + 27.94, Y - 8.89), (X + 27.94, Y - 6.35), None))
W(P(ro, '2'), P('J10', 'T')); s.noconnect(P('J10', 'TN'))
p = P('J10', 'S'); gnd_left(p)

# ---------------------------------------------------------------- CV INPUTS
s.box('CV INPUTS: +-10 V -> 3.3..0 V (inverting); V/OCT +-5 V at 0.1 % to its own ADC pin', g(4), g(60), g(82), g(124))
x0, y0 = g(10), g(65)
for k, (top, bot, net, txt) in enumerate((('16k', '12k', 'CV_BIAS', 'CV_BIAS = 3V3A x 12k / 28k = 1.41 V'),
                                          ('20k', '12k', 'VOCT_BIAS', 'VOCT_BIAS = 3V3A x 12k / 32k = 1.24 V'))):
    x = x0 + k * g(30)
    pw('+3.3VA', (x, y0)); Rv(top, x, y0 + 3.81, 'l', d='bias divider'); Rv(bot, x, y0 + 11.43, 'l', d='bias divider')
    pw('GND', (x, y0 + 15.24))
    W((x, y0 + 7.62), (x + 7.62, y0 + 7.62), (x + 15.24, y0 + 7.62)); Cv('1u', x + 7.62, y0 + 11.43, 'r'); pw('GND', (x + 7.62, y0 + 15.24))
    lab(net, (x + 15.24, y0 + 7.62))
    s.text(txt, x - g(2), y0 + g(8.5))
CV = [('J4', 'SEL', 'CV_SEL', 'U3', 1), ('J5', 'SPEED CV', 'CV_SPEED', 'U3', 2), ('J6', 'FORM CV', 'CV_FORM', 'U3', 3),
      ('J7', 'BEND CV', 'CV_BEND', 'U3', 4), ('J3', 'V/OCT', 'ADC_VOCT', 'U4', 1)]
for k, (jr, jl, net, u, unit) in enumerate(CV):
    X, Y = g(46), g(78) + k * g(9)
    prec = net == 'ADC_VOCT'
    pl, mi, ou = OP(u, 'TL074', unit, X, Y)
    W(pl, (X - 10.16, pl[1]), (X - 10.16, pl[1] - 2.54)); lab('VOCT_BIAS' if prec else 'CV_BIAS', (X - 10.16, pl[1] - 2.54), 'l')
    jack(jr, jl, X - 35.56, Y + 2.54, mirror='x')
    rin = Rh('100k 0.1%' if prec else '100k', X - 17.78, Y + 2.54, 'b', d='V/OCT input, 0.1 %' if prec else 'CV input')
    W(P(jr, 'T'), P(rin, '1')); W(P(rin, '2'), mi)
    p = P(jr, 'S'); W(p, (p[0] + 2.54, p[1])); pw('GND', (p[0] + 2.54, p[1]))
    s.noconnect(P(jr, 'TN'))
    rf = Rh('33k 0.1%' if prec else '16k', X, Y + 7.62, 'b', d='V/OCT gain 0.33, 0.1 %' if prec else 'CV gain 0.16')
    W((X - 10.16, mi[1]), (X - 10.16, Y + 7.62), P(rf, '1')); W(P(rf, '2'), (X + 10.16, Y + 7.62), (X + 10.16, Y))
    ro = Rh('1k', X + 15.24, Y, 'b', d='ADC series / clamp R')
    W(ou, P(ro, '1'))
    A = (X + 22.86, Y)
    d = nx('D')
    s.part(d, 'Diode', 'BAT54S', 'BAT54S', A[0], Y - 5.08, 0, fp='Package_TO_SOT_SMD:SOT-23', lcsc='C19726', desc='clamp to 0..3V3A', text='t')
    W(P(ro, '2'), A, (X + 30.48, Y)); lab(net, (X + 30.48, Y))
    pw('GND', P(d, '1'), 'up'); pw('+3.3VA', P(d, '2'))

# ---------------------------------------------------------------- GATES + EOW
s.box('GATES: NPN inverters (threshold ~1.2 V); EOW: x2.8 trigger, 0 / 9.2 V', g(86), g(60), g(150), g(124))
for k, (jr, jl, net) in enumerate((('J1', 'TRIG', 'GATE_TRIG'), ('J2', 'RESET', 'GATE_RESET'))):
    X, Y = g(124), g(72) + k * g(17)
    q = nx('Q')
    s.part(q, 'Transistor_BJT', 'MMBT3904', 'MMBT3904', X + 7.62, Y, 0, fp='Package_TO_SOT_SMD:SOT-23', lcsc='C20526', desc='gate inverter',
           text=((X + 12.7, Y - 1.27), (X + 12.7, Y + 1.27), 'left'))
    jack(jr, jl, X - 30.48, Y, mirror='x')
    rb = Rh('100k', X - 17.78, Y, 't', d='gate input')
    W(P(jr, 'T'), P(rb, '1')); W(P(rb, '2'), P(q, '1'))
    s.noconnect(P(jr, 'TN'))
    p = P(jr, 'S'); W(p, (p[0] + 2.54, p[1])); pw('GND', (p[0] + 2.54, p[1]))
    rp = Rv('100k', X - 10.16, Y + 6.35, 'l', d='gate divider: threshold ~1.2 V'); W((X - 10.16, Y), P(rp, '1')); pw('GND', P(rp, '2'))
    dd = nx('D')
    s.part(dd, 'Device', 'D', '1N4148W', X - 5.08, Y + 6.35, 270, fp='Diode_SMD:D_SOD-123', lcsc='C81598', desc='negative base clamp', text='r')
    W((X - 5.08, Y), P(dd, '1')); pw('GND', P(dd, '2'))
    pw('GND', P(q, '2'))
    cc = P(q, '3'); W(cc, (cc[0], cc[1] - 2.54))
    ru = Rv('10k', cc[0], cc[1] - 6.35, 'r', d='collector pull-up'); pw('+3V3', P(ru, '1'))
    W((cc[0], cc[1] - 2.54), (cc[0] + 7.62, cc[1] - 2.54)); lab(net, (cc[0] + 7.62, cc[1] - 2.54))
# EOW
X, Y = g(116), g(112)
pl, mi, ou = OP('U4', 'TL074', 2, X, Y)
W(pl, (X - 15.24, pl[1]), (X - 17.78, pl[1])); lab('EOW_GPIO', (X - 17.78, pl[1]), 'l')
rd = Rv('100k', X - 15.24, pl[1] + 3.81, 'l', d='pull-down: EOW rests at 0 V while the Pico boots'); pw('GND', P(rd, '2'))
W(mi, (X - 10.16, mi[1]), (X - 10.16, Y + 7.62))
rg = Rv('10k', X - 10.16, Y + 11.43, 'r', d='EOW gain'); pw('GND', P(rg, '2'))
rf = Rh('18k', X, Y + 7.62, 'b', d='EOW trigger gain x2.8 -> 9.2 V')
W((X - 10.16, Y + 7.62), P(rf, '1')); W(P(rf, '2'), (X + 10.16, Y + 7.62), (X + 10.16, Y))
ro = Rh('1k', X + 15.24, Y, 't', d='output series R'); W(ou, P(ro, '1'))
jack('J9', 'EOW', X + 27.94, Y, rot=180, text=((X + 27.94, Y - 8.89), (X + 27.94, Y - 6.35), None))
W(P(ro, '2'), P('J9', 'T')); s.noconnect(P('J9', 'TN'))
p = P('J9', 'S'); gnd_left(p)
# unused TL074 units 3, 4 of U4: + to ground, output to -
for k, unit in enumerate((3, 4)):
    X, Y = g(96) + k * g(24), g(98)
    pl, mi, ou = OP('U4', 'TL074', unit, X, Y)
    W(pl, (pl[0] - 2.54, pl[1])); pw('GND', (pl[0] - 2.54, pl[1]), 'left')
    W(mi, (mi[0] - 2.54, mi[1]), (mi[0] - 2.54, Y + 5.08), (ou[0] + 2.54, Y + 5.08), (ou[0] + 2.54, Y), ou)
s.text('U4 units 3 / 4 unused: follower to ground.', g(88), g(103))

# ---------------------------------------------------------------- MUXES, POTS, BUTTONS, ENCODER, OLED, LED
s.box('CONTROLS: 2 x 74HC4051 on 3V3A (shared selects), pots, buttons, encoder, OLED, LED', g(154), g(60), g(326), g(124))
MUXES = (('U6', 'ADC_MUX_A', {'13': 'POT_PITCH', '14': 'POT_SPEED', '15': 'POT_FORMANT', '12': 'POT_BEND',
                              '1': 'POT_BLEND', '5': 'POT_INTONE', '2': 'BTN_BACK', '4': 'BTN_ENC'}),
         ('U7', 'ADC_MUX_B', {'13': 'CV_SEL', '14': 'CV_SPEED', '15': 'CV_FORM', '12': 'CV_BEND',
                              '1': 'BTN_SPEAK', '5': 'CARRIER_DET', '2': 'MUX_B6', '4': 'MUX_B7'}))
for k, (u, com, ch) in enumerate(MUXES):
    UX, UY = g(170) + k * g(36), g(78)
    ic(u, '74xx', '74HC4051', '74HC4051', UX, UY, fp='Package_SO:SOIC-16_3.9x9.9mm_P1.27mm', lcsc='C132065',
       desc='8-ch analog mux', text=((UX + 5.08, UY + 20.32), (UX + 5.08, UY + 22.86), 'left'))
    term(u, '3', com)
    for n, net in (('11', 'MUX_S0'), ('10', 'MUX_S1'), ('9', 'MUX_S2')): term(u, n, net)
    term(u, '6', 'GND')
    p = P(u, '16'); W(p, (p[0], p[1] - 2.54)); pw('+3.3VA', (p[0], p[1] - 2.54))
    W((p[0], p[1] - 2.54), (p[0] + 7.62, p[1] - 2.54)); Cv('100n', p[0] + 7.62, p[1] - 6.35, 'r'); pw('GND', (p[0] + 7.62, p[1] - 10.16), 'up')
    pg, pv = P(u, '8'), P(u, '7')
    W(pg, (pg[0], pg[1] + 2.54)); W(pv, (pv[0], pv[1] + 2.54)); W((pg[0], pg[1] + 2.54), (pv[0], pv[1] + 2.54)); pw('GND', (pg[0], pg[1] + 2.54))
    for n, net in ch.items(): term(u, n, net)
s.text('Mux A: X0-X5 = PITCH SPEED FORMANT BEND BLEND INTONE, X6 BACK, X7 encoder push.\n'
       'Mux B: X0-X3 = SEL SPEED FORM BEND CV, X4 SPEAK, X5 CARRIER_DET, X6-X7 spare.', g(158), g(94))
for k, net in enumerate(('MUX_B6', 'MUX_B7')):
    x = g(228) + k * g(6)
    lab(net, (x, g(84)), 'r'); W((x, g(84)), (x, g(85.5)))
    Rv('100k', x, g(85.5) + 3.81, 'r', d='spare mux input: defined at 0 V'); pw('GND', (x, g(85.5) + 7.62))
# pots
KN = ['PITCH', 'SPEED', 'FORMANT', 'BEND', 'BLEND', 'INTONE']
for i, k in enumerate(KN):
    x, y = g(160) + i * g(12), g(108)
    rv = f'RV{i + 1}'
    s.part(rv, 'Device', 'R_Potentiometer', f'{k} B10K', x, y, 180, fp=FPOT,
           desc=f'Alpha RD901F 9 mm B10K, panel: {k}', text='r', extra={'Panel': k})
    pw('+3.3VA', P(rv, '3')); pw('GND', P(rv, '1'))
    stub(P(rv, '2'), f'POT_{k}', -2.54)
s.text('CW end = pin 3 = 3V3A: the reading rises clockwise.', g(158), g(115))
# buttons
for i, (sw, b, net) in enumerate((('SW1', 'BACK', 'BTN_BACK'), ('SW2', 'SPEAK', 'BTN_SPEAK'))):
    x, y = g(244) + i * g(16), g(104)
    s.part(sw, 'Switch', 'SW_Push', b, x, y, 0, fp=FBTN, desc=f'12 mm round tact (DAIER TSL-A064), panel: {b}', text='t', extra={'Panel': b})
    p1 = P(sw, '1'); W(p1, (p1[0] - 2.54, p1[1]), (p1[0] - 2.54, p1[1] - 2.54))
    ru = Rv('10k', p1[0] - 2.54, p1[1] - 6.35, 'l', d='button pull-up (mux on 3V3A)'); pw('+3.3VA', P(ru, '1'))
    stub((p1[0] - 2.54, p1[1]), net, -2.54)
    gnd_right(P(sw, '2'))
# encoder
EX, EY = g(266), g(76)
s.part('SW3', 'Device', 'RotaryEncoder_Switch_MP', 'EC11 encoder', EX, EY, 0,
       fp='Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm', desc='EC11 with push', text=((EX, EY - 10.16), (EX, EY - 7.62), None))
# A: pull-up and debounce cap go up; B: go down. A 10k x 10n = 0.1 ms.
for pin_, net, up in (('A', 'ENC_A', True), ('B', 'ENC_B', False)):
    p = P('SW3', pin_); xr_ = p[0] - 10.16; sg = -1 if up else 1
    W(p, (xr_ - 12.7, p[1])); lab(net, (xr_ - 12.7, p[1]), 'l')
    Rv('10k', xr_, p[1] + sg * 3.81, 'r', d='encoder pull-up'); pw('+3V3', (xr_, p[1] + sg * 7.62), 'up' if up else 'down')
    Cv('10n', xr_ - 7.62, p[1] + sg * 3.81, 'l', d='encoder debounce'); pw('GND', (xr_ - 7.62, p[1] + sg * 7.62), 'up' if up else 'down')
pw('GND', P('SW3', 'C'), 'left')
p = P('SW3', 'S1'); W(p, (p[0] + 2.54, p[1]), (p[0] + 2.54, p[1] - 2.54))
ru = Rv('10k', p[0] + 2.54, p[1] - 6.35, 'r', d='encoder push pull-up (mux on 3V3A)'); pw('+3.3VA', P(ru, '1'))
stub((p[0] + 2.54, p[1]), 'BTN_ENC', 5.08)
gnd_right(P('SW3', 'S2'))
pw('GND', P('SW3', 'MP'))
# OLED
OX, OY = g(300), g(76)
s.part('DS1', 'CVSpeak', 'OLED_SH1106_SPI', 'SH1106 1.3in SPI', OX, OY, 0, fp='CVSpeak:OLED_SH1106_1.3in_SPI_7pin',
       desc='1.3in 128x64 OLED module, SH1106, 7-pin SPI', text=((OX, OY - 12.7), (OX, OY + 12.7), None))
pw('GND', P('DS1', '1'), 'left'); pw('+3V3', P('DS1', '2'), 'left')
for n, net in (('3', 'OLED_SCK'), ('4', 'OLED_MOSI'), ('5', 'OLED_RES'), ('6', 'OLED_DC')): stub(P('DS1', n), net, -7.62)
pw('GND', P('DS1', '7'), 'left')
s.text('CS tied low (only SPI device).\nCheck the module pin order: some swap GND / VCC.', OX - g(8), OY + g(7))
# LED
LX, LY = g(300), g(104)
lab('LED_SPEAK', (LX - g(6), LY), 'l')
rl = Rh('1k', LX - g(3), LY, 't', d='LED current ~1.3 mA')
W((LX - g(6), LY), P(rl, '1'))
s.part('D_LED1', 'Device', 'LED', 'SPEAKING', LX + g(3), LY, 180, fp='LED_THT:LED_D3.0mm', desc='3 mm LED, panel: SPEAKING', text='t',
       extra={'Panel': 'SPEAKING'})
W(P(rl, '2'), P('D_LED1', '2')); gnd_right(P('D_LED1', '1'))

# ---------------------------------------------------------------- OP-AMP SUPPLIES
s.box('OP-AMP SUPPLIES (100n per pin, at the package)', g(216), g(128), g(270), g(156))
for k, (u, sym, unit, vp, vn) in enumerate((('U3', 'TL074', 5, '4', '11'), ('U4', 'TL074', 5, '4', '11'),
                                            ('U5', 'OPA1678', 3, '8', '4'))):
    X, Y = g(228) + k * g(14), g(142)
    fp, d_, lc = OPS[sym]
    s.part(u, 'Amplifier_Operational', sym, sym, X, Y, 0, unit=unit, fp=fp, lcsc=lc, desc=d_, text=((X + 2.54, Y - 1.27), (X + 2.54, Y + 1.27), 'left'))
    a, b = P(u, vp), P(u, vn)
    W(a, (a[0], a[1] - 5.08)); pw('+12V', (a[0], a[1] - 5.08))
    W(b, (b[0], b[1] + 5.08)); pw('-12V', (b[0], b[1] + 5.08), 'down')
    W((a[0], a[1] - 5.08), (a[0] - 5.08, a[1] - 5.08)); Cv('100n', a[0] - 5.08, a[1] - 1.27, 'l'); pw('GND', (a[0] - 5.08, a[1] + 2.54))
    W((b[0], b[1] + 5.08), (b[0] - 5.08, b[1] + 5.08)); Cv('100n', b[0] - 5.08, b[1] + 8.89, 'l'); pw('GND', (b[0] - 5.08, b[1] + 12.7))

# ---------------------------------------------------------------- TEST POINTS
s.box('TEST POINTS (back of the board): rails, ADC pins, codec, I2S, clocks, gates, spares', g(4), g(128), g(130), g(156))
TPS = [['+12V', '-12V', '+5V', '+3V3', '+3.3VA', 'VSYS'],
       ['GND', 'GND', 'CV_BIAS', 'VOCT_BIAS', 'ADC_VOCT', 'ADC_MUX_A'],
       ['ADC_MUX_B', 'CODEC_IN_L', 'CODEC_OUT_L', 'CODEC_MCLK', 'I2S_BCLK', 'I2S_LRCK'],
       ['I2S_DOUT', 'I2S_DIN', 'I2C_SDA', 'I2C_SCL', 'GATE_TRIG', 'GATE_RESET'],
       ['EOW_GPIO', 'CARRIER_DET', 'RUN', 'SPARE_15', 'SPARE_20', 'SPARE_22'],
       ['MUX_B6', 'MUX_B7']]
for c, col in enumerate(TPS):
    for r, net in enumerate(col):
        x, y = g(16) + c * g(19), g(133) + r * g(3)
        tp = nx('TP')
        gnd = net == 'GND'
        s.part(tp, 'Connector', 'TestPoint', net, x, y, 270,
               fp='TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm' if gnd else 'TestPoint:TestPoint_Pad_D1.5mm',
               desc=f'test point {net}', text=((x + 4.45, y - 1.27), (x + 4.45, y + 1.27), 'left'), bom=False)
        if net in POWER: pw(net, P(tp, '1'), 'left')
        else: stub(P(tp, '1'), net, -2.54)

# ---------------------------------------------------------------- NOTES
s.text('OPEN (v0.0)\n'
       '- ES8388 reference caps: 10u + 100n each on VREF / ADCVREF / VMID. Check against the datasheet figure.\n'
       '- LP5907MFX-3.3: LCSC part and stock to check (or another low-noise 3.3 V LDO in SOT-23-5).\n'
       '- OLED module: measure the outline, header and active area; then draw CVSpeak:OLED_SH1106_1.3in_SPI_7pin.\n'
       '- LCSC numbers marked NEW in build_sch.py: verify.\n'
       '- Layout: codec and audio op-amp away from the buck; one ground plane, codec AGND and DGND tied under the chip.',
       g(276), g(132), 1.5)

s.write(OUT, TITLE)
print('wrote', OUT, len(s.syms), 'symbols')
