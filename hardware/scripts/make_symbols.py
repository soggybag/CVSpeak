"""Writes CVSpeak.kicad_sym (project symbol library). Plain python3, from the hardware folder:
   python3 scripts/make_symbols.py
OLED_SH1106_SPI: the common 7-pin 1.3" SPI module (GND VCC D0 D1 RES DC CS). Check the pin order on the module
you buy: some 1.3" boards swap GND and VCC.
Pico2: KiCad's MCU_Module:RaspberryPi_Pico (same pinout), with GND / AGND typed power_in (the stock symbol has them as
power_out, which ERC flags against every GND symbol). The ES8388 comes straight from KiCad's Audio library."""
import sys
sys.path.insert(0, 'scripts')
from lib import flat
from sx import Sym, dump, find, find1


def pin(num, name, x, y, ang, typ='bidirectional'):
    return (f'      (pin {typ} line (at {x} {y} {ang}) (length 2.54)\n'
            f'        (name "{name}" (effects (font (size 1.27 1.27))))\n'
            f'        (number "{num}" (effects (font (size 1.27 1.27)))))\n')


def props(ref, val, fp, desc, ry):
    out = ''
    for k, v, at, hide in (('Reference', ref, f'0 {ry} 0', ''), ('Value', val, f'0 {-ry} 0', ''),
                           ('Footprint', fp, '0 0 0', ' (hide yes)'), ('Datasheet', '', '0 0 0', ' (hide yes)'),
                           ('Description', desc, '0 0 0', ' (hide yes)')):
        out += f'    (property "{k}" "{v}" (at {at}){hide} (effects (font (size 1.27 1.27))))\n'
    return out


s = '(kicad_symbol_lib (version 20241209) (generator "make_symbols.py") (generator_version "9.0")\n'
s += '  (symbol "OLED_SH1106_SPI" (exclude_from_sim no) (in_bom yes) (on_board yes)\n'
s += props('DS', 'SH1106 1.3in SPI', 'CVSpeak:OLED_SH1106_1.3in_SPI_7pin', '1.3in 128x64 OLED module, SH1106, 7-pin SPI', 11.43)
s += ('    (symbol "OLED_SH1106_SPI_0_1"\n      (rectangle (start -7.62 10.16) (end 7.62 -10.16) '
      '(stroke (width 0.254) (type default)) (fill (type background)))\n    )\n')
s += '    (symbol "OLED_SH1106_SPI_1_1"\n'
for i, (nm, t) in enumerate([('GND', 'power_in'), ('VCC', 'power_in'), ('D0 SCK', 'input'), ('D1 MOSI', 'input'),
                             ('RES', 'input'), ('DC', 'input'), ('CS', 'input')], 1):
    s += pin(i, nm, -10.16, round(7.62 - (i - 1) * 2.54, 2), 0, t)
s += '    )\n  )\n'
pico = flat('MCU_Module', 'RaspberryPi_Pico')
pico[1] = 'Pico2'
for u in find(pico, 'symbol'):
    u[1] = 'Pico2' + u[1][len('RaspberryPi_Pico'):]
    for p in find(u, 'pin'):
        if find1(p, 'name')[1] in ('GND', 'AGND'): p[1] = Sym('power_in')
for p in find(pico, 'property'):
    if p[1] == 'Value': p[2] = 'Pico 2'
    if p[1] == 'Description': p[2] = 'Raspberry Pi Pico 2 (RP2350), same pinout as the Pico'
s += '  ' + dump(pico, 1) + '\n)\n'
open('CVSpeak.kicad_sym', 'w').write(s)
print('wrote CVSpeak.kicad_sym')
