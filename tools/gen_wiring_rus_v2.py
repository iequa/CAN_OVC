from html import escape
import os

W, H = 3200, 2440
out = []
out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
out.append('<rect width="100%" height="100%" fill="#ffffff"/>')
out.append('''<style>
  text { font-family: "Segoe UI", Arial, sans-serif; fill: #1b2631; }
  .title { font-size: 46px; font-weight: 700; }
  .sub { font-size: 21px; fill: #566573; }
  .section { font-size: 26px; font-weight: 700; fill: #1f618d; }
  .box-title { font-size: 21px; font-weight: 700; }
  .small { font-size: 17px; }
  .tiny { font-size: 15px; fill: #566573; }
  .pin { font-size: 16px; }
  .net { font-size: 16px; font-weight: 700; }
</style>''')

# ---------- helpers ----------
def text(x, y, s, cls="", anchor="start", fill=None, size=None, weight=None):
    attrs = [f'x="{x}"', f'y="{y}"']
    if cls:
        attrs.append(f'class="{cls}"')
    if anchor != "start":
        attrs.append(f'text-anchor="{anchor}"')
    if fill:
        attrs.append(f'fill="{fill}"')
    if size:
        attrs.append(f'font-size="{size}"')
    if weight:
        attrs.append(f'font-weight="{weight}"')
    out.append(f'<text {" ".join(attrs)}>{escape(s)}</text>')

def box(x, y, w, h, title, lines=(), stroke="#2c3e50", fill="#fbfcfc", line_size=17):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="2.5"/>')
    text(x + 16, y + 30, title, "box-title")
    yy = y + 58
    for line in lines:
        text(x + 16, yy, line, "small", size=line_size)
        yy += 23

def wire(points, color="#566573", width=2.5):
    p = " ".join(f"{x},{y}" for x, y in points)
    out.append(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round" stroke-linecap="round"/>')

def dot(x, y, color="#566573"):
    out.append(f'<circle cx="{x}" cy="{y}" r="5" fill="{color}"/>')

def gnd(x, y, label=None):
    out.append(f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y+12}" stroke="#566573" stroke-width="2.5"/>')
    out.append(f'<line x1="{x-13}" y1="{y+12}" x2="{x+13}" y2="{y+12}" stroke="#566573" stroke-width="2.5"/>')
    out.append(f'<line x1="{x-8}" y1="{y+17}" x2="{x+8}" y2="{y+17}" stroke="#566573" stroke-width="2.5"/>')
    out.append(f'<line x1="{x-3}" y1="{y+22}" x2="{x+3}" y2="{y+22}" stroke="#566573" stroke-width="2.5"/>')
    if label:
        text(x + 20, y + 22, label, "tiny")

def pwr(x, y, label, color="#d35400", direction="up"):
    if direction == "up":
        y2 = y - 26
        out.append(f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y2}" stroke="{color}" stroke-width="2.5"/>')
        out.append(f'<polygon points="{x},{y2-2} {x-6},{y2+10} {x+6},{y2+10}" fill="{color}"/>')
        text(x + 12, y2 + 8, label, "small", fill=color, weight="700")
    else:
        y2 = y + 26
        out.append(f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y2}" stroke="{color}" stroke-width="2.5"/>')
        out.append(f'<polygon points="{x},{y2+2} {x-6},{y2-10} {x+6},{y2-10}" fill="{color}"/>')
        text(x + 12, y2, label, "small", fill=color, weight="700")

def pin(x, y):
    out.append(f'<circle cx="{x}" cy="{y}" r="4.5" fill="#2c3e50"/>')

def section(x, y, w, h, title):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="16" fill="none" stroke="#aab7b8" stroke-width="2.5" stroke-dasharray="10 7"/>')
    text(x + 22, y + 36, title, "section")

# ---------- title ----------
text(60, 72, "CAN_OVC — схема разводки адаптера", "title")
text(60, 108, "Vectra C full CAN · MS-CAN 95.238 кбит/с · CID/GID + CD70/DVD90 · Ревизия C", "sub")
text(3140, 72, "ТОЛЬКО ПРИЁМ", "sub", anchor="end", fill="#c0392b", weight="700")

# legend
lx = 60
ly = 138
text(lx, ly, "Обозначения:", "tiny", weight="700")
out.append(f'<line x1="{lx+130}" y1="{ly-5}" x2="{lx+180}" y2="{ly-5}" stroke="#d35400" stroke-width="3"/>')
text(lx + 188, ly, "питание", "tiny", fill="#d35400")
out.append(f'<line x1="{lx+270}" y1="{ly-5}" x2="{lx+320}" y2="{ly-5}" stroke="#2471a3" stroke-width="3"/>')
text(lx + 328, ly, "CAN", "tiny", fill="#2471a3")
out.append(f'<line x1="{lx+380}" y1="{ly-5}" x2="{lx+430}" y2="{ly-5}" stroke="#7d3c98" stroke-width="3"/>')
text(lx + 438, ly, "управление / реле", "tiny", fill="#7d3c98")
out.append(f'<line x1="{lx+610}" y1="{ly-5}" x2="{lx+660}" y2="{ly-5}" stroke="#1e8449" stroke-width="3"/>')
text(lx + 668, ly, "аудио", "tiny", fill="#1e8449")
dot(lx + 760, ly - 5)
text(lx + 772, ly, "соединение", "tiny")
text(lx + 900, ly, "пересечение без точки — без соединения", "tiny")

# ---------- sections ----------
section(50, 170, 1000, 760, "1. ПИТАНИЕ И ЗАЩИТА")
section(1100, 560, 680, 840, "2. ESP32")
section(1900, 560, 620, 400, "3. CAN СО СТОРОНЫ АВТОМОБИЛЯ")
section(1900, 1020, 1240, 620, "4. ШЛЮЗ / CID / РЕЛЕ")
section(50, 980, 1000, 560, "5. ACC / ЗАЖИГАНИЕ")
section(1100, 1620, 1550, 460, "6. АУДИО / AUX")
section(50, 2140, 3090, 270, "7. ПРОВЕРКА И ЗАМЕТКИ")

# ---------- 1. POWER ----------
# input chain
box(90, 220, 210, 120, "J1 АВТО 12 В", ["ACC +12 В", "GND / масса"], "#d35400")
pin(300, 260); pin(300, 310)
box(360, 235, 110, 60, "F1", ["1 А"], "#d35400")
box(520, 220, 200, 90, "Q1", ["защита от", "переполюсовки"], "#d35400")
box(810, 320, 110, 90, "D1", ["TVS", "SMBJ18A"], "#d35400")
pin(850, 320); pin(850, 410)

wire([(300, 260), (360, 260)], "#d35400")
wire([(470, 260), (520, 260)], "#d35400")
wire([(720, 260), (780, 260)], "#d35400")
dot(780, 260, "#d35400")
wire([(780, 260), (850, 260), (850, 320)], "#d35400")
gnd(850, 410, "GND")
# +12V output terminal
wire([(780, 260), (980, 260), (980, 300)], "#d35400")
out.append('<circle cx="980" cy="300" r="6" fill="#d35400"/>')
text(996, 306, "+12 В (на реле)", "small", fill="#d35400", weight="700")

# regulators
box(120, 440, 260, 140, "U1 Mini360", ["12 В → 5 В", "3 А, настроить 5.0 В"], "#d35400")
pin(120, 480); pin(380, 510); pin(250, 580)
box(760, 440, 240, 140, "U3 MCP1700", ["5 В → 3.3 В", "только для DAC"], "#d35400")
pin(760, 480); pin(1000, 510); pin(880, 580)

# +12V node to U1 input and to 12V cap group
wire([(780, 260), (780, 420), (100, 420), (100, 620), (120, 620)], "#d35400")
wire([(120, 420), (120, 480)], "#d35400")
dot(120, 420, "#d35400")
dot(120, 620, "#d35400")
# U1 out to +5V rail
wire([(380, 510), (420, 510), (420, 620)], "#d35400")
# U1 gnd (route left of the capacitor group)
wire([(250, 580), (250, 590), (110, 590), (110, 800), (120, 800)], "#566573")
dot(120, 800, "#566573")
# U3 in from +5V rail (vertical left of the 3.3V group)
wire([(660, 620), (660, 480), (760, 480)], "#d35400")
dot(660, 600, "#d35400")
# U3 out to +3.3V rail
wire([(1000, 510), (1030, 510), (1030, 620), (1000, 620)], "#d35400")
dot(1000, 620, "#d35400")
# U3 gnd (route left of the 3.3 V group)
wire([(880, 580), (880, 590), (710, 590), (710, 800)], "#566573")
dot(710, 800, "#566573")

# capacitor groups: rails
# 12V group
wire([(120, 620), (360, 620)], "#d35400")
wire([(120, 760), (360, 760)], "#566573")
text(120, 642, "+12 В", "small", fill="#d35400", weight="700")
# 5V group
wire([(420, 620), (660, 620)], "#d35400")
wire([(420, 760), (660, 760)], "#566573")
text(420, 642, "+5 В", "small", fill="#d35400", weight="700")
# 3.3V group
wire([(720, 620), (1000, 620)], "#d35400")
wire([(720, 760), (1000, 760)], "#566573")
text(720, 642, "+3.3 В", "small", fill="#d35400", weight="700")

# capacitors
caps = [
    (160, "C1", ["1000 мкФ", "35 В"]),
    (260, "C2", ["100 нФ", "50 В"]),
    (500, "C3", ["470 мкФ", "16 В"]),
    (780, "C4", ["1 мкФ", "25 В"]),
    (880, "C5", ["10 мкФ", "25 В"]),
]
for cx, name, lines in caps:
    box(cx, 650, 80, 80, name, lines, "#d35400", line_size=15)
    wire([(cx + 40, 620), (cx + 40, 650)], "#d35400")
    wire([(cx + 40, 730), (cx + 40, 760)], "#566573")
    dot(cx + 40, 620, "#d35400")
    dot(cx + 40, 760, "#566573")

# group grounds to bus
wire([(240, 760), (240, 800)], "#566573")
wire([(540, 760), (540, 800)], "#566573")
wire([(860, 760), (860, 800)], "#566573")
wire([(120, 800), (1020, 800)], "#566573")
for gx in (120, 240, 540, 710, 860):
    dot(gx, 800, "#566573")
gnd(1020, 800, "GND")

# +5V to ESP32
wire([(660, 620), (660, 600), (1080, 600), (1080, 700), (1100, 700)], "#d35400")
dot(660, 620, "#d35400")
text(690, 592, "+5 В → ESP32", "small", fill="#d35400", weight="700")

text(90, 880, "5 В: ESP32 и MCP2515. 12 В: только катушка реле. 3.3 В: PCM5102A, SN65, TXS, подтяжка ACC.", "tiny")
text(90, 904, "Керамика — X7R на 50 В; электролиты 105 °C. C1 — 35–50 В, C3 — 16 В.", "tiny")

# ---------- 2. ESP32 ----------
out.append('<rect x="1100" y="600" width="680" height="760" rx="12" fill="#f4f6f7" stroke="#2c3e50" stroke-width="2.5"/>')
text(1120, 640, "ESP32 DevKit · 4 МБ", "box-title")
text(1120, 668, "питание 5 В / GND, лог 3.3 В", "tiny")

left_pins = [("5 В", 700), ("GND", 750), ("3.3 В", 800), ("GPIO34", 980), ("GPIO32", 1060)]
right_pins = [("GPIO17 CAN_TX", 680), ("GPIO16 CAN_RX", 730),
              ("GPIO18 SPI_SCK", 860), ("GPIO19 SPI_MISO", 905),
              ("GPIO23 SPI_MOSI", 950), ("GPIO21 SPI_CS", 995),
              ("GPIO22 MCP_INT", 1040)]
bottom_pins = [("GPIO26 BCLK", 1240), ("GPIO25 LRCK", 1350), ("GPIO27 DIN", 1460)]

for name, y in left_pins:
    text(1120, y + 6, name, "pin")
    pin(1100, y)
for name, y in right_pins:
    text(1760, y + 6, name, "pin", anchor="end")
    pin(1780, y)
for name, x in bottom_pins:
    text(x, 1345, name, "pin", anchor="middle", size=15)
    pin(x, 1360)
    out.append(f'<line x1="{x}" y1="1360" x2="{x}" y2="1390" stroke="#2c3e50" stroke-width="2"/>')

# ESP32 power
wire([(1100, 700), (1080, 700)], "#d35400")
gnd(1070, 750)
wire([(1100, 750), (1070, 750)], "#566573")
wire([(1100, 800), (1075, 800), (1075, 762)], "#d35400")
out.append('<polygon points="1075,760 1069,772 1081,772" fill="#d35400"/>')
text(1055, 758, "3.3 В", "small", anchor="end", fill="#d35400", weight="700")

# ---------- 3. CAN CAR ----------
box(1900, 620, 250, 200, "U4 SN65HVD230", ["сторона авто", "VCC 3.3 В", "120 Ом удалить", "RS → GND"], "#2471a3")
pin(1900, 650); pin(1900, 680); pin(1900, 730)
pin(2150, 720); pin(2150, 770)
pin(1950, 820)
text(1888, 674, "TXD", "pin", anchor="end")
text(1888, 724, "RXD", "pin", anchor="end")
text(2162, 714, "CANH", "pin")
text(2162, 764, "CANL", "pin")

wire([(1780, 680), (1900, 680)], "#2471a3")
wire([(1780, 730), (1900, 730)], "#2471a3")
# VCC 3.3 V stub
wire([(1860, 650), (1900, 650)], "#d35400")
out.append('<polygon points="1900,650 1888,644 1888,656" fill="#d35400"/>')
text(1848, 645, "3.3 В", "small", anchor="end", fill="#d35400", weight="700")
gnd(1950, 820, "GND")

box(2230, 640, 220, 160, "J2 MS-CAN АВТО", ["OBD 3 (H)", "OBD 11 (L)", "или CID 19/20"], "#2471a3")
pin(2230, 700); pin(2230, 750)
pin(2450, 700); pin(2450, 750)

wire([(2150, 720), (2190, 720), (2190, 700), (2230, 700)], "#2471a3")
wire([(2150, 770), (2210, 770), (2210, 750), (2230, 750)], "#2471a3")

text(1900, 900, "Шина авто уже терминирована. Не подключать обе пары MS-CAN.", "tiny")

# ---------- 4. GATEWAY / CID / RELAY ----------
# TXS
box(1900, 1100, 250, 240, "U5 TXS0108E", [], "#7d3c98")
txs_y = [1150, 1190, 1230, 1270, 1310]
txs_names = ["A1 SCK", "A2 MISO", "A3 MOSI", "A4 CS", "A5 INT"]
b_names = ["B1", "B2", "B3", "B4", "B5"]
for i, y in enumerate(txs_y):
    pin(1900, y); pin(2150, y)
    text(1912, y + 6, txs_names[i], "pin")
    text(2138, y + 6, b_names[i], "pin", anchor="end")
text(2025, 1334, "сдвиг 3.3 В ↔ 5 В", "tiny", anchor="middle")
pin(1950, 1100); pin(1950, 1340)
pwr(1950, 1100, "5 В (VB)")
pwr(2050, 1100, "3.3 В (VA)")
gnd(1950, 1340, "GND")

# MCP
box(2230, 1100, 250, 240, "U6 MCP2515 + TJA1050", [], "#2471a3")
mcp_names = ["SCK", "MISO", "MOSI", "CS", "INT"]
for i, y in enumerate(txs_y):
    pin(2230, y)
    text(2242, y + 6, mcp_names[i], "pin")
pin(2480, 1180); pin(2480, 1230)
text(2468, 1174, "CANH", "pin", anchor="end")
text(2468, 1224, "CANL", "pin", anchor="end")
text(2242, 1334, "VDD 5 В · 120 Ом", "tiny")
pin(2280, 1100); pin(2280, 1340)
pwr(2280, 1100, "5 В")
gnd(2280, 1340, "GND")

# SPI ESP32 -> TXS
spi = [
    (860, 1860, 1150),
    (905, 1845, 1190),
    (950, 1830, 1230),
    (995, 1815, 1270),
    (1040, 1800, 1310),
]
for py, vx, ty in spi:
    wire([(1780, py), (vx, py), (vx, ty), (1900, ty)], "#7d3c98")

# TXS -> MCP
for y in txs_y:
    wire([(2150, y), (2230, y)], "#7d3c98")

# MCP -> K1 NO
wire([(2480, 1180), (2540, 1180), (2540, 1200), (2600, 1200)], "#2471a3")
wire([(2480, 1230), (2570, 1230), (2570, 1250), (2600, 1250)], "#2471a3")

# K1
box(2600, 1050, 300, 300, "K1 Fujitsu RY12W-K", ["DPDT, 2 перекидных", "катушка 12 В / 960 Ом", "NC — bypass", "NO — через шлюз", "coil+ / coil− — снизу"], "#7d3c98")
pin(2700, 1050); pin(2750, 1050)
pin(2600, 1200); pin(2600, 1250)
pin(2900, 1150); pin(2900, 1200)
pin(2650, 1350); pin(2750, 1350)
text(2694, 1022, "NC1", "pin", anchor="end")
text(2756, 1022, "NC2", "pin")
text(2588, 1194, "NO1", "pin", anchor="end")
text(2588, 1244, "NO2", "pin", anchor="end")
text(2912, 1144, "COM1", "pin")
text(2912, 1194, "COM2", "pin")
text(2640, 1372, "coil+", "pin", anchor="end")
text(2760, 1372, "coil−", "pin")

# car bus -> K1 NC
wire([(2450, 700), (2650, 700), (2650, 1030), (2700, 1030), (2700, 1050)], "#2471a3")
wire([(2450, 750), (2600, 750), (2600, 1010), (2750, 1010), (2750, 1050)], "#2471a3")
text(2470, 690, "H", "tiny", fill="#2471a3")
text(2470, 740, "L", "tiny", fill="#2471a3")

# K1 COM -> CID
wire([(2900, 1150), (2980, 1150)], "#2471a3")
wire([(2900, 1200), (2980, 1200)], "#2471a3")

box(2980, 1120, 150, 140, "J3 CID CAN", ["H", "L", "120 Ом внутри"], "#2471a3")
pin(2980, 1150); pin(2980, 1200)

# coil driver
text(2640, 1360, "coil+", "tiny", anchor="end", fill="#7d3c98")
text(2760, 1360, "coil−", "tiny", fill="#7d3c98")

# +12 V to coil+
out.append('<polygon points="2650,1352 2644,1364 2656,1364" fill="#7d3c98"/>')
wire([(2650, 1420), (2650, 1350)], "#7d3c98")
dot(2650, 1420, "#7d3c98")
text(2638, 1414, "+12 В", "small", anchor="end", fill="#7d3c98", weight="700")

# coil- to Q2 collector
wire([(2750, 1350), (2750, 1440)], "#7d3c98")

# D2 across coil
box(2680, 1365, 60, 60, "D2", ["1N4007"], "#7d3c98", line_size=13)
pin(2710, 1365); pin(2710, 1425)
wire([(2650, 1365), (2710, 1365)], "#7d3c98")
wire([(2710, 1425), (2750, 1425)], "#7d3c98")
dot(2650, 1365, "#7d3c98")
dot(2750, 1425, "#7d3c98")

# Q2
box(2700, 1440, 100, 90, "Q2", ["2N2222", "NPN"], "#7d3c98", line_size=15)
pin(2750, 1440); pin(2700, 1480); pin(2750, 1530)
gnd(2750, 1530, "GND")

# R3
box(2560, 1455, 90, 60, "R3", ["1 кОм"], "#7d3c98", line_size=15)
pin(2560, 1480); pin(2650, 1480)
wire([(2650, 1480), (2700, 1480)], "#7d3c98")

# GPIO32 -> R3
wire([(1100, 1060), (1070, 1060), (1070, 1480), (2560, 1480)], "#7d3c98")
text(2400, 1472, "GPIO32 → драйвер реле", "tiny", anchor="end", fill="#7d3c98")

# bypass note
text(1900, 1628, "Реле обесточено — CID напрямую к авто. Включено — CID через шлюз.", "tiny")

# ---------- 5. ACC ----------
box(90, 1040, 210, 120, "J5 ACC", ["12 В зажигания", "GND"], "#7d3c98")
pin(300, 1080); pin(300, 1120)

box(350, 1055, 100, 50, "R1", ["4.7 кОм"], "#7d3c98")
pin(350, 1080); pin(450, 1080)

box(500, 1030, 220, 140, "U8 PC817", ["LED 1–2", "выход 4–3", "оптрон"], "#7d3c98")
pin(500, 1080); pin(500, 1130)
pin(720, 1080); pin(720, 1130)

box(780, 1000, 70, 80, "R2", ["10 кОм"], "#7d3c98")
pin(815, 1000); pin(815, 1080)

box(780, 1150, 70, 80, "C6", ["100 нФ"], "#7d3c98")
pin(815, 1150); pin(815, 1230)

wire([(300, 1080), (350, 1080)], "#7d3c98")
wire([(450, 1080), (500, 1080)], "#7d3c98")
# collector node = R2 bottom
wire([(720, 1080), (815, 1080)], "#7d3c98")
dot(815, 1080, "#7d3c98")
wire([(815, 1150), (815, 1080)], "#7d3c98")
# output to GPIO34
wire([(815, 1080), (900, 1080), (900, 980), (1100, 980)], "#7d3c98")
text(905, 970, "GPIO34", "tiny", fill="#7d3c98")
# R2 pull-up
pwr(815, 1000, "+3.3 В")

# grounds
wire([(300, 1120), (300, 1300)], "#566573")
wire([(500, 1130), (470, 1130), (470, 1300)], "#566573")
wire([(720, 1130), (720, 1300)], "#566573")
wire([(815, 1230), (815, 1300)], "#566573")
wire([(300, 1300), (900, 1300)], "#566573")
for gx in (470, 720, 815):
    dot(gx, 1300, "#566573")
gnd(900, 1300, "GND")

text(90, 1370, "ACC = 12 В → LED открыт → фототранзистор открыт → GPIO34 = LOW.", "small")
text(90, 1398, "R1 задаёт ~2.3 мА через LED. R2 — подтяжка к 3.3 В. C6 — фильтр помех.", "small")
text(90, 1426, "Оптрон развязывает 12 В и GPIO34: на входе ESP32 нет 12 В и выбросов.", "small")
text(90, 1454, "В прошивке учесть инверсию и антидребезг 200–500 мс.", "small")

# ---------- 6. AUDIO ----------
box(1500, 1660, 300, 240, "U7 PCM5102A", ["3.3 В (чистый LDO)", "GND", "BCLK / LRCK / DIN", "MCLK → GND", "Left / Right"], "#1e8449")
pin(1500, 1700); pin(1500, 1750); pin(1500, 1800)
pin(1800, 1740); pin(1800, 1790); pin(1800, 1840)
pin(1650, 1660); pin(1650, 1900)

pwr(1650, 1660, "3.3 В (MCP1700)")
gnd(1650, 1900, "GND")

wire([(1240, 1390), (1240, 1700), (1500, 1700)], "#1e8449")
wire([(1350, 1390), (1350, 1750), (1500, 1750)], "#1e8449")
wire([(1460, 1390), (1460, 1800), (1500, 1800)], "#1e8449")

box(1900, 1660, 300, 240, "Делитель 2:1", ["C7/C8 10 мкФ 25 В", "R4/R5 10 кОм", "R6/R7 10 кОм → GND", "на каждый канал"], "#1e8449")
pin(1900, 1740); pin(1900, 1790); pin(1900, 1840)
pin(2200, 1740); pin(2200, 1790); pin(2200, 1840)
pin(2050, 1900); pin(2050, 1660)

wire([(1800, 1740), (1900, 1740)], "#1e8449")
wire([(1800, 1790), (1900, 1790)], "#1e8449")
wire([(1800, 1840), (1900, 1840)], "#1e8449")

box(2300, 1680, 320, 200, "J4 AUX МАГНИТОЛЫ", ["L", "R", "GND", "проверить pinout"], "#1e8449")
pin(2300, 1740); pin(2300, 1790); pin(2300, 1840)

wire([(2200, 1740), (2300, 1740)], "#1e8449")
wire([(2200, 1790), (2300, 1790)], "#1e8449")
wire([(2200, 1840), (2300, 1840)], "#1e8449")

# audio ground rail
wire([(1650, 1900), (1650, 1960)], "#566573")
wire([(2050, 1900), (2050, 1960)], "#566573")
wire([(1550, 1960), (2100, 1960)], "#566573")
for gx in (1650, 2050):
    dot(gx, 1960, "#566573")
gnd(1550, 1960, "GND (аудио)")

text(1500, 2035, "PCM5102A: только 3.3 В, MCLK → GND. Выход ~2 В RMS; AUX обычно 0.5–1 В RMS.", "tiny")
text(1500, 2060, "C7/C8 — разделительные, R4/R6 и R5/R7 — делитель 2:1 на каждый канал.", "tiny")

# ---------- 7. NOTES ----------
notes_left = [
    "Питание: 12 В → F1 1 А → Q1 (защита) → D1 (TVS) → Mini360 5 В → MCP1700 3.3 В.",
    "Проверить 5.0 В и 3.3 В мультиметром до подключения ESP32 и модулей.",
    "Общая земля обязательна; аудиоземлю вести отдельно от силовой.",
    "SN65HVD230: выпаять 120 Ом, RS → GND. Шина авто уже терминирована.",
    "MCP2515: 120 Ом на модуле + 120 Ом CID ≈ 60 Ом при включённом реле.",
    "TXS0108E обязателен: MCP2515 работает с 5-вольтовой логикой.",
]
notes_right = [
    "Реле обесточено (GPIO32 = 0) — CID напрямую к авто, безопасный bypass.",
    "K1 RY12W-K: DPDT, катушка 12 В / 960 Ом (12.5 мА), контакты 1 А.",
    "Первый запуск в авто: только приём, без 0x691 и 0x6C1.",
    "Снять лог 0x6C1, 0x691, 0x501, 0x201, 0x206, 0x506.",
    "Проверить AUX на CD70/DVD90; если AUX нет — нужно другое решение.",
    "Реле RY12W-K не коммутирует питание, только сигнальные CAN H/L.",
]
for i, s in enumerate(notes_left):
    text(80, 2200 + i * 26, "• " + s, "small")
for i, s in enumerate(notes_right):
    text(1580, 2200 + i * 26, "• " + s, "small")

text(60, 2396, "Файл: docs/wiring_rus_v2.svg · Подробные таблицы и BOM: docs/WIRING_rus_v2.md", "tiny")

out.append('</svg>')

os.makedirs("docs", exist_ok=True)
with open("docs/wiring_rus_v2.svg", "w", encoding="utf-8") as f:
    f.write("\n".join(out))
print("docs/wiring_rus_v2.svg written, size", W, H)
