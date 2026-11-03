from html import escape
import os

W, H = 1900, 1400
out = []
out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
out.append('<rect width="100%" height="100%" fill="#ffffff"/>')
out.append('''<style>
  text { font-family: "Segoe UI", Arial, sans-serif; fill: #1b2631; }
  .title { font-size: 32px; font-weight: 700; }
  .sub { font-size: 15px; fill: #566573; }
  .section { font-size: 19px; font-weight: 700; fill: #1f618d; }
  .box-title { font-size: 15px; font-weight: 700; }
  .small { font-size: 12px; }
  .tiny { font-size: 11px; fill: #566573; }
  .power { stroke: #d35400; }
  .can { stroke: #2471a3; }
  .audio { stroke: #1e8449; }
  .ctrl { stroke: #7d3c98; }
  .net { font-size: 12px; font-weight: 700; fill: #7f8c8d; }
</style>''')

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

def rect(x, y, w, h, title, lines=(), stroke="#2c3e50", fill="#fbfcfc", title_cls="box-title"):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
    text(x+12, y+24, title, title_cls)
    yy = y+46
    for line in lines:
        text(x+12, yy, line, "small")
        yy += 18

def wire(points, color="#566573", width=2, dash=None, label=None, lx=0, ly=0):
    p = " ".join(f"{x},{y}" for x, y in points)
    d = f' stroke-dasharray="{dash}"' if dash else ""
    out.append(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{width}"{d} stroke-linejoin="round" stroke-linecap="round"/>')
    if label:
        text(lx, ly, label, "tiny", fill=color)

def net(x, y, s, color="#7f8c8d", anchor="start"):
    text(x, y, s, "net", anchor=anchor, fill=color)

def section(x, y, w, h, title):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="none" stroke="#aab7b8" stroke-width="2" stroke-dasharray="8 5"/>')
    text(x+16, y+28, title, "section")

# Title
text(40, 48, "CAN_OVC — схема разводки адаптера", "title")
text(40, 78, "Vectra C full CAN · MS-CAN 95.238 kbit/s · CID/GID + CD70/DVD90 · Rev A 2026-10-31", "sub")
text(1660, 48, "listen-only", "sub", fill="#c0392b", weight="700")

# Sections
section(30, 100, 650, 450, "1. ПИТАНИЕ И ЗАЩИТА")
section(700, 100, 330, 520, "2. ESP32")
section(1060, 100, 500, 250, "3. CAN СО СТОРОНЫ АВТО")
section(1060, 360, 810, 330, "4. GATEWAY / CID")
section(30, 620, 630, 220, "5. ACC / ЗАЖИГАНИЕ")
section(700, 660, 800, 310, "6. AUDIO / AUX")
section(30, 1000, 1840, 350, "7. ПРОВЕРКА И ЗАМЕТКИ")

# Power chain
rect(50, 150, 160, 70, "J1", ["CAR 12V ACC", "GND"], "#d35400")
rect(240, 160, 70, 50, "F1", ["1A"], "#d35400")
rect(330, 160, 75, 50, "D1", ["TVS 18V"], "#d35400")
rect(430, 160, 100, 50, "Q1", ["reverse prot."], "#d35400")
rect(50, 260, 190, 90, "U1 Mini360 #1", ["12V -> 5.0V", "3A", "set exactly 5.0V"], "#d35400")
rect(270, 260, 190, 90, "U2 Mini360 #2", ["spare / second 5V rail"], "#d35400")
rect(270, 390, 190, 80, "U3 MCP1700", ["5V -> 3.3V", "DAC only"], "#d35400")
rect(50, 390, 90, 60, "C1", ["1000uF", "25V"], "#d35400")
rect(150, 390, 90, 60, "C2", ["100nF"], "#d35400")
rect(50, 475, 120, 50, "C3", ["470uF/10V"], "#d35400")
rect(470, 390, 120, 80, "C4/C5", ["1uF in", "10uF+100nF"], "#d35400")

wire([(210,185),(240,185)], "#d35400")
wire([(310,185),(330,185)], "#d35400")
wire([(405,185),(430,185)], "#d35400")
wire([(530,185),(560,185),(560,240),(145,240),(145,260)], "#d35400")
net(560, 232, "+12V", "#d35400")
wire([(145,350),(145,390),(270,390)], "#d35400")
net(150, 382, "+5V rail", "#d35400")
wire([(460,430),(470,430)], "#d35400")
net(470, 380, "+3.3V rail", "#d35400")
text(50, 535, "5V: ESP32, MCP2515, relay coil.   3.3V: PCM5102A only (clean LDO).", "tiny")

# ESP32
out.append('<rect x="720" y="150" width="290" height="440" rx="12" fill="#f4f6f7" stroke="#2c3e50" stroke-width="2"/>')
text(735, 178, "ESP32 DevKit classic", "box-title")
text(735, 196, "4 MB flash · USB", "tiny")

left_pins = [("5V",190),("GND",220),("3V3",250),("GPIO34",310),("GPIO32",340)]
right_pins = [("GPIO17 CAN_TX",180),("GPIO16 CAN_RX",210),
              ("GPIO18 SPI_SCK",270),("GPIO19 SPI_MISO",300),
              ("GPIO23 SPI_MOSI",330),("GPIO21 SPI_CS",360),
              ("GPIO22 MCP_INT",390),("GPIO26 I2S_BCLK",450),
              ("GPIO25 I2S_LRCK",480),("GPIO27 I2S_DATA",510)]
for name,y in left_pins:
    text(735, y, name, "small")
    out.append(f'<circle cx="720" cy="{y-4}" r="3" fill="#2c3e50"/>')
for name,y in right_pins:
    text(995, y, name, "small", anchor="end")
    out.append(f'<circle cx="1010" cy="{y-4}" r="3" fill="#2c3e50"/>')



# Car side CAN
rect(1080, 145, 210, 140, "U4 SN65HVD230", ["car side", "VCC 3.3V", "TXD/RXD -> ESP32", "CANH/L -> J2", "120R REMOVED"], "#2471a3")
rect(1330, 160, 200, 80, "J2 CAR MS-CAN", ["OBD 3(H)/11(L)", "or CID 19(L)/20(H)"], "#2471a3")
wire([(1010,180),(1045,180),(1045,200),(1080,200)], "#2471a3")
wire([(1010,210),(1055,210),(1055,225),(1080,225)], "#2471a3")
wire([(1290,200),(1330,200)], "#2471a3")
wire([(1290,225),(1330,225)], "#2471a3")
net(1030, 172, "CAN_TX", "#2471a3")
net(1030, 238, "CAN_RX", "#2471a3")
text(1080, 310, "Авто-шина уже терминирована. Не подключать обе пары MS-CAN.", "tiny")

# Gateway
rect(1080, 410, 180, 180, "U5 TXS0108E", ["VA 3.3V / VB 5V", "OE 3.3V", "A1..A5: ESP32", "B1..B5: MCP2515"], "#7d3c98")
rect(1300, 410, 220, 160, "U6 MCP2515 + TJA1050", ["VDD 5V", "8 MHz", "120R onboard", "CANH/L -> K1 NO"], "#2471a3")
rect(1560, 410, 180, 160, "K1 DPDT relay", ["COM -> CID", "NC -> car", "NO -> MCP/TJA", "coil 5V"], "#7d3c98")
rect(1770, 410, 110, 120, "J3", ["CID CAN", "H / L"], "#2471a3")
rect(1560, 600, 180, 60, "Q2 + D2 + R3", ["GPIO32 -> 1k", "coil driver"], "#7d3c98")

wire([(1010,330),(1040,330),(1040,500),(1080,500)], "#7d3c98", label="SPI 3.3V (5 lines)", lx=1045, ly=322)
wire([(1260,500),(1300,500)], "#7d3c98", label="SPI 5V", lx=1265, ly=492)
wire([(1520,470),(1560,470)], "#2471a3", label="CAN H/L", lx=1525, ly=462)
wire([(1530,200),(1540,200),(1540,440),(1560,440)], "#2471a3", label="car CAN H/L -> NC", lx=1545, ly=430)
wire([(1740,470),(1770,470)], "#2471a3", label="COM -> CID", lx=1745, ly=462)
net(1080, 405, "3.3V logic", "#7d3c98")
net(1210, 405, "5V logic", "#7d3c98")

# ACC
rect(50, 670, 160, 70, "J5 ACC", ["12V", "GND"], "#7d3c98")
rect(240, 680, 70, 50, "R1", ["10k"], "#7d3c98")
rect(340, 660, 160, 100, "U8 PC817", ["LED 1-2", "out 4-3"], "#7d3c98")
rect(530, 680, 70, 50, "R2", ["10k"], "#7d3c98")
rect(530, 750, 70, 50, "C6", ["100nF"], "#7d3c98")
wire([(210,705),(240,705)], "#7d3c98")
wire([(310,705),(340,705)], "#7d3c98")
wire([(500,690),(530,690)], "#7d3c98", label="GPIO34", lx=505, ly=682)
wire([(500,775),(530,775)], "#7d3c98")
text(50, 825, "ACC=12V -> PC817 ON -> GPIO34 LOW. Pull-up 10k to 3.3V.", "tiny")

# Audio
rect(720, 710, 240, 190, "U7 PCM5102A", ["3V3 (clean LDO)", "GND", "BCLK 26 / LR 25", "DATA 27", "MCLK -> GND", "Left / Right out"], "#1e8449")
rect(1010, 730, 230, 150, "R1/R2 + C", ["10uF -> R1 10k", "R2 10k -> GND", "per channel", "2:1 attenuator"], "#1e8449")
rect(1300, 730, 220, 100, "J4 RADIO AUX", ["L / R / GND", "verify CD70/DVD90 pinout"], "#1e8449")
wire([(1010,450),(1040,450)], "#1e8449")
net(1042, 445, "I2S 26/25/27", "#1e8449")
wire([(700,760),(720,760)], "#1e8449")
net(698, 752, "I2S 26/25/27", "#1e8449", anchor="end")
wire([(960,800),(1010,800)], "#1e8449", label="L/R", lx=965, ly=792)
wire([(1240,800),(1300,800)], "#1e8449")
wire([(365,470),(365,520),(680,520),(680,690),(700,690),(700,710)], "#d35400")
text(720, 950, "PCM5102A: только 3.3V, MCLK=GND. Output ~2 Vrms; AUX usually 0.5-1 Vrms.", "tiny")

# Notes
notes_left = [
    "ПИТАНИЕ: 12V ACC -> F1 1A -> TVS -> reverse protection -> Mini360 5.0V.",
    "Mini360 output проверить мультиметром ДО подключения ESP32.",
    "MCP1700 3.3V — только PCM5102A; ESP32 питать от 5V.",
    "Общая земля обязательна. Аудиоземлю вести отдельно от силовой.",
    "CAN CAR: SN65HVD230, 120R выпаять. RS -> GND.",
    "CAN CID: MCP2515/TJA, 120R onboard + 120R дисплея = ~60R.",
    "Level shifter TXS0108E обязателен: MCP2515 работает от 5V.",
    "Реле K1: обесточено = CID напрямую к авто; включено = через gateway.",
    "При сне/сбросе GPIO32=0 -> реле обесточено -> bypass.",
]
notes_right = [
    "ПЕРВЫЙ ЗАПУСК В АВТО: только listen-only, без 0x691/0x6C1.",
    "Снять лог 0x6C1, 0x691, 0x501, 0x201, 0x206, 0x506.",
    "Не подключать обе пары MS-CAN одновременно.",
    "Проверить наличие AUX на CD70/DVD90. Если AUX нет — нужно отдельное решение.",
    "PCM5102A: MCLK -> GND; FMT/DEMP/XSMT уже заданы платой.",
    "Аудио: 10uF + 10k/10k на канал; при клиппинге увеличить R1.",
    "ESP32: USB и 5V одновременно не подавать (если нет диода на плате).",
    "Корпус: антенна ESP32 не должна быть за металлом.",
]
for i, s in enumerate(notes_left):
    text(55, 1045 + i*24, "• " + s, "small")
for i, s in enumerate(notes_right):
    text(960, 1045 + i*24, "• " + s, "small")

text(55, 1290, "Файл: docs/wiring.svg · Подробные таблицы: docs/WIRING.md", "tiny")
out.append('</svg>')

os.makedirs("docs", exist_ok=True)
with open("docs/wiring.svg", "w", encoding="utf-8") as f:
    f.write("\n".join(out))
print("docs/wiring.svg written")
