# SPDX-License-Identifier: MPL-2.0
"""MASQUE: the user manual as a PDF (reportlab, matplotlib, Pillow), from the repository's root:
    tools/build_tools.sh                                  # build/gen_masks: the masks' waveforms for the figures
    CC=gcc python3 fm1-base/tools/build.py . --host
    build/host/masque_host tests/host/basic.txt build/host  # the screenshots
    python3 docs/manual/make_manual.py                    # docs/manual/MASQUE-manual.pdf
"""
import os, subprocess
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "manual")
SHOTS = os.path.join(ROOT, "build", "host")
FIG = os.path.join(ROOT, "build", "manual")
os.makedirs(FIG, exist_ok=True)
os.chdir(FIG)
with open("waves.bin", "wb") as w:
    w.write(subprocess.run([os.path.join(ROOT, "build", "gen_masks"), "-w"], check=True,
                           capture_output=True).stdout)
exec(open(os.path.join(OUT, "figs.py")).read())
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Image, Table, TableStyle,
                                PageBreak, KeepTogether, NextPageTemplate)
from reportlab.platypus.tableofcontents import TableOfContents
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from PIL import Image as PILImage

F = os.path.join(ROOT, "fm1-base", "assets", "fonts") + "/"
pdfmetrics.registerFont(TTFont("Barlow", F + "BarlowSemiCondensed-Bold.ttf"))
pdfmetrics.registerFont(TTFont("BarlowM", F + "BarlowSemiCondensed-SemiBold.ttf"))
D = "/usr/share/fonts/truetype/dejavu/"
pdfmetrics.registerFont(TTFont("Body", D + "DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("BodyB", D + "DejaVuSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("BodyI", D + "DejaVuSans-Oblique.ttf"))
pdfmetrics.registerFont(TTFont("Mono", D + "DejaVuSansMono.ttf"))
from reportlab.pdfbase.pdfmetrics import registerFontFamily
registerFontFamily("Body", normal="Body", bold="BodyB", italic="BodyI", boldItalic="BodyB")

RED = colors.HexColor("#d8281c")
INK = colors.HexColor("#1d1a1a")
GREY = colors.HexColor("#6b6464")
PALE = colors.HexColor("#f6eeec")
RULE = colors.HexColor("#d9cfcc")

body = ParagraphStyle("body", fontName="Body", fontSize=9.6, leading=14.2, textColor=INK, spaceAfter=6)
small = ParagraphStyle("small", parent=body, fontSize=8.4, leading=11.5, textColor=GREY)
cell = ParagraphStyle("cell", parent=body, fontSize=8.6, leading=11.6, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="BodyB")
h1 = ParagraphStyle("h1", fontName="Barlow", fontSize=24, leading=28, textColor=INK, spaceBefore=0, spaceAfter=10)
h2 = ParagraphStyle("h2", fontName="Barlow", fontSize=14.5, leading=18, textColor=RED, spaceBefore=12, spaceAfter=5,
                    keepWithNext=1)
cap = ParagraphStyle("cap", parent=small, alignment=TA_CENTER, spaceBefore=3, spaceAfter=10)
note = ParagraphStyle("note", parent=body, backColor=PALE, borderPadding=(7, 9, 7, 9), leftIndent=9, rightIndent=9,
                      spaceBefore=6, spaceAfter=12)

W = A4[0] - 40 * mm


class Doc(BaseDocTemplate):
    def __init__(self, fn):
        super().__init__(fn, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=20 * mm,
                         bottomMargin=20 * mm, title="MASQUE — User Manual", author="MASQUE contributors",
                         subject="A bitmask synthesizer for the M-VAVE FM-1")
        fr = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="f")
        self.addPageTemplates([PageTemplate("cover", [fr], onPage=self.cover),
                               PageTemplate("body", [fr], onPage=self.page)])

    def afterFlowable(self, f):
        if isinstance(f, Paragraph) and f.style.name in ("h1", "h2"):
            lvl = 0 if f.style.name == "h1" else 1
            key = "h%d" % id(f)
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(f.getPlainText(), key, level=lvl)
            self.notify("TOCEntry", (lvl, f.getPlainText(), self.page, key))

    @staticmethod
    def cover(c, d):
        c.saveState()
        c.setFillColor(colors.HexColor("#0b0809"))
        c.rect(0, 0, A4[0], A4[1], fill=1, stroke=0)
        c.restoreState()

    @staticmethod
    def page(c, d):
        c.saveState()
        c.setStrokeColor(RULE)
        c.setLineWidth(0.6)
        c.line(20 * mm, A4[1] - 13 * mm, A4[0] - 20 * mm, A4[1] - 13 * mm)
        c.setFont("Barlow", 9)
        c.setFillColor(RED)
        c.drawString(20 * mm, A4[1] - 11 * mm, "MASQUE")
        c.setFillColor(GREY)
        c.setFont("BarlowM", 9)
        c.drawRightString(A4[0] - 20 * mm, A4[1] - 11 * mm, "User Manual")
        c.drawCentredString(A4[0] / 2, 11 * mm, str(d.page))
        c.restoreState()


def P(t, s=body):
    return Paragraph(t, s)


def img(path, width, caption=None):
    w, h = PILImage.open(path).size
    out = [Image(path, width=width, height=width * h / w)]
    if caption:
        out.append(P(caption, cap))
    return KeepTogether(out)


def table(rows, widths, head=True):
    data = [[P(c, cellb if (head and i == 0) or (j == 0 and not head) else cell) for j, c in enumerate(r)]
            for i, r in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
          ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
          ("LEFTPADDING", (0, 0), (-1, -1), 5), ("RIGHTPADDING", (0, 0), (-1, -1), 5)]
    if head:
        st += [("BACKGROUND", (0, 0), (-1, 0), PALE), ("LINEBELOW", (0, 0), (-1, 0), 0.8, RED)]
    t.setStyle(TableStyle(st))
    return t


def knobs(rows):
    """a page's rows of values: Row | K1 | K2 | K3 | K4"""
    return table([["Row", "K1", "K2", "K3", "K4"]] + rows, [12 * mm] + [(W - 12 * mm) / 4] * 4)


S = []
# ------------------------------------------------------------------------------------------------- cover
S.append(Spacer(1, 52 * mm))
S.append(P('<font color="#ff3424">MASQUE</font>', ParagraphStyle("t", fontName="Barlow", fontSize=64, leading=70,
                                                                  alignment=TA_CENTER)))
S.append(P("A bitmask synthesizer for the M-VAVE FM-1", ParagraphStyle(
    "st", fontName="BarlowM", fontSize=17, leading=22, textColor=colors.HexColor("#e8e2df"), alignment=TA_CENTER)))
S.append(Spacer(1, 10 * mm))
S.append(Image(os.path.join(SHOTS, "home_playing.png"), width=70 * mm, height=70 * mm))
S.append(Spacer(1, 12 * mm))
S.append(P("USER MANUAL", ParagraphStyle("um", fontName="Barlow", fontSize=15, leading=20, alignment=TA_CENTER,
                                         textColor=colors.HexColor("#ff3424"))))
S.append(P("The synthesis method, the panel, every page and how to play it", ParagraphStyle(
    "um2", fontName="Body", fontSize=9.5, leading=14, alignment=TA_CENTER, textColor=colors.HexColor("#a89f9c"))))
S.append(NextPageTemplate("body"))
S.append(PageBreak())

# ---------------------------------------------------------------------------------------------- contents
S.append(P("Contents", ParagraphStyle("ct", parent=h1)))
toc = TableOfContents()
toc.levelStyles = [ParagraphStyle("t0", fontName="BarlowM", fontSize=10.5, leading=13, leftIndent=0, spaceBefore=1),
                   ParagraphStyle("t1", fontName="Body", fontSize=7.8, leading=9.4, leftIndent=12, textColor=GREY)]
S.append(toc)
S.append(PageBreak())

# ------------------------------------------------------------------------------------------------ intro
S.append(P("1  Introduction", h1))
S.append(P("MASQUE turns the M-VAVE FM-1 into a bitmask synthesizer modelled on Kodamo's MASK1. Each voice has two "
           "<b>bitmask oscillators</b> and a <b>sample-and-hold noise</b>, mixed into a <b>resonant state-variable "
           "filter</b>. Four loopable envelopes shape them; a mask envelope on each oscillator and a pitch envelope "
           "move them; two LFOs, velocity, the mod wheel and the keys' pressure modulate them. Nine play modes "
           "respond to how you play: legato, staccato, how far apart the keys are. An arpeggiator, a looper and two "
           "effects complete the instrument."))
S.append(P("MASQUE is an independent project. It is not made, endorsed or supported by Kodamo; MASK1 is Kodamo's. "
           "MASQUE contains no Kodamo code, sounds or data: its oscillator was rebuilt from what Kodamo and the "
           "MASK1's manual say the instrument does.", note))

S.append(table([["", ""],
                ["Voices", "10 by default (4 to 12), two bitmask oscillators and a noise each"],
                ["Oscillators", "512 masks each, plus three sub-masks between each pair; 4x oversampled"],
                ["Filter", "2-pole state-variable with DRIVE and saturating resonance: LP / HP / BP / NOTCH / PEAK, "
                           "and a tuned comb (COMB+ / COMB−); key tracking; 24 dB or a fixed second filter (EXTRA)"],
                ["Envelopes", "4 loopable ADSRs per voice (OSC 1, OSC 2, NOISE, FILTER); delta envelopes for each "
                              "oscillator's mask and for pitch"],
                ["LFOs", "2, eight waves, 16 destinations, delay and decay"],
                ["Modulation", "an 8-row matrix in each sound: 13 sources and any CC, 26 destinations"],
                ["MPE", "RP-053: zones, Member and Master Channels, bend sensitivity, MPE+; DIN or USB host"],
                ["Tuning", "A 300–500.99 Hz; 5–72 equal steps to the octave, or four historical temperaments"],
                ["Play modes", "POLY, MONO, SLUR, MONO PORTA, PARA, MOLY, PARA SLUR, MONO RETRIG, POLY RETRIG"],
                ["Arpeggiator", "26 patterns, 20–400 BPM or MIDI clock, HOLD, TRIG"],
                ["Looper", "one track, up to 4096 events"],
                ["Effects", "FX1: chorus / flanger / phaser, distortion, bitcrusher, ring mod. FX2: delay, plate "
                            "reverb, distortion"],
                ["Sounds", "40 factory (P00–P39), 128 user (U000–U127), backed up over USB"]][1:],
               [32 * mm, W - 32 * mm], head=False))
S.append(Spacer(1, 6))
S.append(img("fig_voice.png", W, "One voice. The oscillators run at four times the sample rate and are brought "
                                 "down through two halfband filters before the noise joins and the filter."))

# ---------------------------------------------------------------------------------------------- synthesis
S.append(PageBreak())
S.append(P("2  Bitmask synthesis", h1))
S.append(P("Most digital oscillators read a stored waveform. A bitmask oscillator has only one: a sine. Everything "
           "else comes from <i>how the sine is read</i>, and that is decided by a single number, the <b>mask</b>. "
           "Kodamo describes it as a sine wave split into sections that are repeated, scaled, inverted or silenced "
           "to build a new waveform. The mask is a 9-bit number, so each oscillator has 512 of them."))
S.append(P("What Kodamo publishes", h2))
S.append(P("Kodamo has not published how the bits map to those operations. What is known: low masks give simple "
           "waveforms and high ones bleepy, raspy, formant-like ones; mask 31 is a sine; 7 a pure saw; 8 a square "
           "whose low half is made of small saws; 20 to 25 are square-like; 56 is a pulse; 78 is a buzz at the "
           "sine's pitch; 28 to 30 are bell-like; and every mask of the form 16k − 1 (15, 31, 47, 63, 79, 95…) is "
           "rounded and mellow. Between two masks lie sub-masks, from the mask value's least significant bits, "
           "which the mask envelope passes through."))
S.append(P("MASQUE's oscillator is one reading of that description, built so that every mask named above comes "
           "out as described."))
S.append(P("Phase and sections", h2))
S.append(P("An oscillator keeps a 32-bit <b>phase</b> that climbs once per cycle of the note. Its top five bits "
           "number 32 equal <b>sections</b> of the cycle (0 to 31); the bits below say where inside the section "
           "the oscillator is. Normally the phase reads straight through a sine table. The mask changes which "
           "phase the table sees."))
S.append(P("The nine bits", h2))
S.append(table([["Bit(s)", "Name", "What it does"],
                ["0–4", "KEEP", "An AND over the phase's five section bits: bit <i>i</i> keeps phase bit 27 + <i>i</i>; "
                                "a cleared bit is forced to 0, so the sections it chose repeat the ones below them."],
                ["5 (+32)", "FLIP", "The sections are read backwards wherever the highest <i>dropped</i> bit of the "
                                    "phase is set."],
                ["6 (+64)", "INVERT", "The sections are turned upside down wherever the dropped bits have odd parity."],
                ["7 (+128)", "SILENCE", "The sections are silenced wherever the lowest dropped bit is set."],
                ["8 (+256)", "REPEAT", "Each kept piece holds three turns of the sine instead of one."]],
               [18 * mm, 20 * mm, W - 38 * mm]))
S.append(Spacer(1, 4))
S.append(P("A dropped bit is one KEEP clears. When nothing is dropped (KEEP = 31), the operations act on the two "
           "halves of the cycle instead, which is why every 16k − 1 mask stays rounded."))
S.append(img("fig_keep.png", W * 0.94, "KEEP alone. The dashed line is the sine for comparison; the grid marks the "
                                       "32 sections."))
S.append(P("Reading the figure: with mask 15 the top section bit is always 0, so sections 16–31 replay sections "
           "0–15, the sine's first half, twice; the result is rounded and an octave up. Mask 7 drops two bits, so "
           "the first quarter of the sine (a rising curve) plays four times: a saw two octaves up. Mask 8 keeps "
           "only one middle bit: for a quarter of the cycle the oscillator replays the sine's very first section "
           "(small rising saws near zero), for the next quarter the section at the sine's peak (nearly flat)."))
S.append(img("fig_ops.png", W, "The operations. Top: one KEEP pattern (11) with FLIP, INVERT and SILENCE added. "
                               "Bottom: the sine, REPEAT on it, the pulse (56) and the buzz (78)."))
S.append(P("Sub-masks", h2))
S.append(P("The mask's position can sit between two whole masks. Its two fraction bits clear phase bits 25 and 26, "
           "below the sections, so each piece grows fine steps: three intermediate waveforms between every pair. "
           "The mask envelope, the LFOs, velocity and aftertouch all move through them; turning MASK by hand moves "
           "whole masks only."))
S.append(P("Level, DC and aliasing", h2))
S.append(P("Many masks are lopsided or quiet. MASQUE removes each mask's DC offset and evens its level (no mask "
           "is louder than a square) from a table measured from the oscillator itself. Because masks have sharp "
           "edges anywhere in the cycle, the oscillators run at four times the sample rate and are filtered back "
           "down, which keeps the aliasing well below the sound."))
S.append(P("Pitch", h2))
S.append(P("Dropping the top section bits repeats part of the cycle, which raises the pitch. Masks below 16 (and "
           "others whose top bit is cleared) sound one or more octaves above the note: 15 and 8 one octave, 7 two, "
           "0 five. Use the oscillator's SEMI to bring them down; the STRINGS sound plays mask 7 at −24."))
S.append(KeepTogether([P("All 512 masks", h2),
                       img(os.path.join(ROOT, "docs", "img", "masks.png"), W, "Every mask, as MASQUE produces it. The 16k − 1 masks are drawn lighter.")]))
S.append(P("A map of the range", h2))
S.append(table([["Mask", "Character"],
                ["31", "the sine"],
                ["15, 47, 63, 79, 95… (16k − 1)", "rounded, mellow (15 and 47 an octave up)"],
                ["28, 29, 30", "the sine with fine steps: bells, marimbas, glassy keys"],
                ["19, 23", "saw-like at the note's pitch"],
                ["7", "a saw, two octaves up"],
                ["8", "a square whose low half is small saws, an octave up"],
                ["20–25", "square-like; 24 a square with saws on its floor"],
                ["40, 56", "pulses"],
                ["78", "a buzz at the note's pitch"],
                ["0–6", "very bright, several octaves up (0: five octaves)"],
                ["128–255", "silences inside the wave: thinner, pulse-like"],
                ["256–511", "three turns of the sine per piece: formants, voices, bleeps"]],
               [55 * mm, W - 55 * mm]))

# ------------------------------------------------------------------------------------------------- panel
S.append(PageBreak())
S.append(P("3  The panel and the screen", h1))
S.append(P("The MASK1 edits one section at a time with a Value knob. The FM-1 has four knobs under its screen, so "
           "MASQUE shows each page's values in <b>rows of four</b>: K1 to K4, left to right. The lit row is the one "
           "the knobs turn; <b>SELECT</b> moves to the next row and on to the next page, so one knob reaches every "
           "value. The buttons jump straight to a section, grouped as on the MASK1."))
S.append(table([["Control", "What it does"],
                ["HOME", "The sound's page: K1 MASK 1, K2 MASK 2, K3 CUTF, K4 RESO."],
                ["SEL", "OSC 1 → OSC 2 → NOISE → FILTER (the four sections with an envelope)."],
                ["ENV", "Lit: the envelope of the section shown instead of its settings, like the MASK1's Env button."],
                ["LFO", "LFO 1 ↔ LFO 2."],
                ["EDIT", "PITCH → MATRIX → GENERAL."],
                ["FX", "The effects."],
                ["ARP", "The arpeggiator. Pressed again on its page: HOLD on / off. Lit while it plays."],
                ["SEQ", "The looper's page."],
                ["REC", "Looper: wait for the first note (blinks), record (lit); again: play the loop."],
                ["PLAY", "Looper: play (green) or stop."],
                ["GLO", "The part's settings (GLOBAL: MIDI, MPE, USB host); again: TUNING."],
                ["SAVE", "Lit while the sound is edited. Press: the WRITE page; press again there: its action."],
                ["SELECT", "The next row, and the next page."],
                ["ALGORITHM", "The one number: MASK of the oscillator shown (OSC 1 on the other pages; on MATRIX, "
                              "the matrix's row). A quick turn "
                              "goes four at a time."],
                ["PRESETS", "The next sound: P00–P39, then U000–U127. It loads when the knob rests."],
                ["K1–K4", "The lit row's four values."],
                ["OCT− / OCT+", "The FM-1's keys an octave of the tuning down / up (−3 to +3); both together: 0."],
                ["MASTER", "Volume."]], [28 * mm, W - 28 * mm]))
S.append(P("The screen", h2))
S.append(P("The <b>top line</b> shows the sound's number and name, a <b>*</b> while it has unsaved edits, and a "
           "dot per voice (lit while sounding). The <b>second line</b> names the page, or for a moment shows what "
           "you just changed; REC, LOOP and ARP appear at the right while active. The <b>middle</b> draws what the "
           "page is about: the masks (moving live with their envelopes and LFOs), an envelope, the filter's "
           "response, an LFO's wave, the effects. The <b>bottom</b> holds the rows of values."))
S.append(img(os.path.join(ROOT, "docs", "img", "screens.png"), W,
             "HOME while playing, OSC 1, OSC 2's envelope, FILTER, the MATRIX, TUNING (31-EDO), FX and WRITE."))
S.append(P("The keys", h2))
S.append(P("The FM-1's 27 keys play with velocity, and each key's <b>pressure</b> is that voice's aftertouch. "
           "They also go out over USB MIDI. Velocity runs through the curve chosen on the GLOBAL page."))

# ---------------------------------------------------------------------------------------------- pages
S.append(PageBreak())
S.append(P("4  The pages", h1))
S.append(P("Ranges are as the screen shows them. Times for A, D, R and the delays: 0 is instant, 30 about 20 ms, "
           "50 about 0.1 s, 70 about 0.8 s, 99 about 12 s.", small))
S.append(P("OSC 1 and OSC 2", h2))
S.append(knobs([["1", "MASK 0–511", "SEMI −60…+60", "TUNE −99…+99 cents", "LEVEL 0–99"],
                ["2", "START −99…+99", "SPD 0–99", "LOOP NO / YES / BIDI", ""]]))
S.append(Spacer(1, 5))
S.append(P("<b>The mask envelope</b> (START, SPD, LOOP) starts each note START masks away from MASK and moves "
           "back to MASK at speed SPD (1 slow, 99 nearly instant, 0 off). LOOP YES starts again from START each "
           "time it arrives (a saw of masks); BIDI goes back and forth (a triangle). It passes through the "
           "sub-masks, so slow sweeps step through the fine intermediate waveforms."))
S.append(P("<b>ENV</b> (each oscillator's level envelope): L level, A attack (linear), D decay (curved), "
           "S sustain, R release (curved), LOOP (attack and decay repeat until the key is let go). LEVEL on row 1 "
           "is the same value as the envelope's L."))
S.append(P("NOISE", h2))
S.append(knobs([["1", "FREQ 0–99", "TRACK 0–99 %", "LEVEL 0–99", ""]]))
S.append(Spacer(1, 5))
S.append(P("A random value held and renewed FREQ times a second: 99 is white noise, lower is crunchier, down to a "
           "few values a second. TRACK makes higher keys renew it faster. ENV gives its envelope."))
S.append(P("FILTER", h2))
S.append(knobs([["1", "CUTF 0–127", "RESO 0–99", "MODE LP / HP / BP / NOTCH", "TRK 0–99 %"],
                ["2", "EXTRA", "ENV −99…+99", "DRIVE 0–99", ""]]))
S.append(Spacer(1, 5))
S.append(P("A two-pole (12 dB/oct) state-variable filter that self-oscillates near the top of RESO. Its resonance "
           "saturates, so pushed hard it growls instead of clipping, and stays bounded. <b>DRIVE</b> overdrives the "
           "sound into it (and into the comb). CUTF is in semitones: 60 is middle C's frequency, 127 about 12.5 kHz. "
           "TRK follows OSC 1's pitch."))
S.append(table([["MODE", "What"],
                ["LP, HP, BP", "low, high and band pass"],
                ["NOTCH", "a narrow cut at the cutoff, deepest with RESO at 0"],
                ["PEAK", "the resonant band added to the sound: nothing is cut, a bump rises at the cutoff"],
                ["COMB+", "a delay line one period of the cutoff long, fed back by RESO: a pitched resonator. Fed "
                          "noise or a short sound it becomes a plucked string or a struck tube (P16 KARPLUS). With "
                          "TRK at 99 it plays the keys' pitches; the filter envelope bends it"],
                ["COMB−", "the same with the feedback inverted: hollower, an octave lower, odd harmonics"]],
               [24 * mm, W - 24 * mm]))
S.append(Spacer(1, 4))
S.append(P("<b>EXTRA</b>: 12DB (one filter), 24DB (a second, clean stage after it: four poles, one peak), LP1–LP7 "
           "or HP1–HP7 (a fixed low or high pass after it: LP1 350 Hz to LP7 9 kHz, HP1 40 Hz to HP7 1.6 kHz; after the comb "
           "too, to darken or thin it). "
           "<b>ENV</b> is how far the filter envelope moves the cutoff, up or down; it is the same value as L on "
           "the filter's ENV page."))
S.append(P("PITCH", h2))
S.append(knobs([["1", "START −48…+48", "SPD 0–40", "END −48…+48", ""]]))
S.append(Spacer(1, 5))
S.append(P("Each note starts START semitones away and glides to its pitch at SPD (1 very slow, 40 almost "
           "instant, 0 off). When the key is let go it glides towards END at the same speed."))
S.append(P("LFO 1 and LFO 2", h2))
S.append(knobs([["1", "AMT 0–99", "SPD 0–99 (0.03–30 Hz)", "DEST", "WAVE"],
                ["2", "DLAY 0–99", "DCAY 0–99", "", ""]]))
S.append(Spacer(1, 5))
S.append(P("Free-running, one for the whole part. <b>DEST</b>: FREQ, VOL, FILT, MASK, PAN, O1 FREQ, O1 VOL, "
           "O1 MASK, O2 FREQ, O2 VOL, O2 MASK, N FREQ, N VOL, FX1 TYP, FX2 TYP, FX2 BAL. <b>WAVE</b>: SIN, SQR, "
           "UP, DOWN, SAWS (two small saws), STEP (four stairs), HSIN (half sines), RAND (sample and hold). "
           "<b>DLAY</b>: after the first key the LFO waits, then fades in (not while the mod wheel or aftertouch "
           "controls its amount or speed). <b>DCAY</b>: it fades out after this long (0 never)."))
S.append(P("MATRIX", h2))
S.append(P("Eight rows, each sending a source to a destination, kept with the sound. Each matrix row takes two rows "
           "of knobs; SELECT walks them, and on this page ALGORITHM jumps from row to row. The page lists all eight "
           "rows and shows the last note's expression live (VEL, PRESS, AT, TIMBRE, BEND; a source not sent yet stays "
           "dark)."))
S.append(table([["Row", "K1", "K2", "K3", "K4"],
                ["A", "SRC", "DEST", "AMT −99…+99", "CURVE"],
                ["B", "LO 0–99", "HI 1–100", "SMOOTH 0–99", "ZONE ALL / LO / HI"]],
               [12 * mm] + [(W - 12 * mm) / 4] * 4))
S.append(Spacer(1, 5))
S.append(table([["Sources", "What"],
                ["VEL, REL VEL", "the note's velocity, and the velocity it is let go with"],
                ["PRESS", "MPE pressure: the note's own Member Channel's pressure"],
                ["AT", "aftertouch: the FM-1's key pressure, poly aftertouch, or a channel's (in MPE the zone's)"],
                ["TIMBRE", "MPE's CC74: the note's own (or its zone's Master Channel's)"],
                ["BEND", "the note's bend, −1…+1"],
                ["WHEEL, CC 0–127", "the mod wheel (CC 1), any controller"],
                ["KEY", "the key, around middle C, −1…+1"],
                ["LFO1, LFO2, F ENV", "the LFOs, the filter envelope"],
                ["RAND", "a random value each note gets as it starts, −1…+1"]],
               [34 * mm, W - 34 * mm]))
S.append(Spacer(1, 5))
S.append(table([["Destinations", "What"],
                ["PITCH, O1 PITCH, O2 PITCH", "up to 24 steps of the tuning"],
                ["MASK, O1 MASK, O2 MASK", "up to 128 masks, through the sub-masks"],
                ["MASK SPD", "the mask envelopes' speed"],
                ["VOL, O1 VOL, O2 VOL, N VOL", "levels, as factors (below)"],
                ["N FREQ, CUTF, RESO, DRIVE", "the noise's rate, the cutoff (up to 72 semitones), resonance, drive"],
                ["F ENV, P ENV", "the filter and pitch envelopes' depth"],
                ["ATK, REL", "the envelopes' attack (at the note's start) and release (as it is let go) times"],
                ["PAN", "the voice's place"],
                ["LFO1 AMT … FX2 BAL", "the part's: the LFOs' amounts and speeds, the effects' balances, from the "
                                       "last note played"]],
               [46 * mm, W - 46 * mm]))
S.append(Spacer(1, 5))
S.append(P("<b>LO</b> and <b>HI</b> pick the part of the source's travel that is used. <b>CURVE</b>: LIN straight, "
           "SOFT quick at the start, HARD slow at the start, S both, BIP for a source that rests in its middle (a CC "
           "at 64). <b>SMOOTH</b> lets the row glide (99: about half a second); it evens out coarse MIDI. "
           "<b>ZONE</b>: in MPE, the notes of the lower zone, the upper, or both. Rows on the same destination add "
           "up. The levels are factors: a positive amount lets the source bring the level up to full, and without "
           "it the level is that much less (VEL > VOL +40: soft notes 40 % quieter); a negative amount takes it down "
           "(PRESS > VOL +99: a note is silent until pressed)."))
S.append(P("A source that has never come (a controller without pressure, a keyboard without MPE) leaves its rows "
           "alone, so a sound made for pressure still plays from a plain keyboard. The factory sounds keep the "
           "MASK1's three routings as their first rows (velocity, the wheel, aftertouch), add MPE pressure beside "
           "aftertouch, and TIMBRE on the masks.", note))
S.append(P("GENERAL", h2))
S.append(knobs([["1", "MODE", "VOL 0–99", "SEMI −24…+24", "PORT 0–13"],
                ["2", "PANS 0–99", "RATE 0–99", "BEND 0–12", ""]]))
S.append(Spacer(1, 5))
S.append(P("<b>PORT</b>: glide time (0 none, 13 very long); the play modes' glides need it above 0, though "
           "SLUR's slides always slide a little. <b>PANS</b>: how widely voices spread left and right. "
           "<b>RATE</b>: envelopes faster up the keyboard and slower down it. <b>BEND</b>: pitch-bend range in "
           "semitones. <b>MODE</b>: the play mode (chapter 5)."))
S.append(P("FX", h2))
S.append(knobs([["1", "1TYP", "1BAL 0–100", "2TYP", "2BAL 0–100"]]))
S.append(Spacer(1, 5))
S.append(table([["Type", "What"],
                ["CH01–CH16", "Chorus; CH11–14 flangers, CH15–16 phasers (FX1)"],
                ["DT01–DT16", "Distortion, gentle to hard; DT13–16 fold the wave (FX1 and FX2)"],
                ["BC01–BC16", "Bitcrusher, fewer bits and a lower rate as the number rises; BC09–16 sweep "
                              "themselves (FX1)"],
                ["RM01–RM16", "RM01–04 a stereo tremolo, then ring modulation from 30 Hz to 2.5 kHz (FX1)"],
                ["DL01–DL16", "Delay, 95 ms to 0.7 s, more feedback higher up; the even numbers ping-pong (FX2)"],
                ["RV01–RV16", "Plate reverb, small and short to large and long (FX2)"]],
               [28 * mm, W - 28 * mm]))
S.append(P("The balances run from 0 (dry) to 100 (all effect). FX1 comes before FX2.", small))
S.append(P("ARP", h2))
S.append(knobs([["1", "TYPE", "SPD 20–400 BPM", "HOLD OFF / ON", "TRIG OFF / ON"]]))
S.append(Spacer(1, 5))
S.append(table([["Patterns", "What"],
                ["1OCT, 2OCT, 3OCT", "up through the held keys over one to three octaves"],
                ["DBL", "each key twice"],
                ["ALT", "lowest, highest, then inwards"],
                ["PONG, PON2, PON3", "up and down over one to three octaves"],
                ["RND, RND2, RND3", "random, over one to three octaves"],
                ["MIN, MAJ, SUS2, SUS4, 7TH", "a chord built on each held key"],
                ["MIN2, MAJ2, SU22, SU42, 7TH2", "the same chords over two octaves"],
                ["1O, 2O, 3O", "each key and its octaves"],
                ["5TH, 5TH2", "each key and its fifth (5TH2: fifth, octave, twelfth)"]],
               [50 * mm, W - 50 * mm]))
S.append(Spacer(1, 4))
S.append(P("Steps are sixteenth notes at SPD; when MIDI clock arrives the arpeggiator follows it. <b>HOLD</b> keeps "
           "the notes after you let go, until you play a new chord. <b>TRIG</b> off: one held note that changes "
           "pitch each step instead of a new note per step."))
S.append(P("GLOBAL", h2))
S.append(knobs([["1", "MIDI OMNI, 1–16, MPE", "VEL CRV 1–6, MAX", "AT CRV 1–6", "VOICES 4–12"],
                ["2", "OCTAVE −3…+3", "USB DEVICE / HOST", "", ""]]))
S.append(Spacer(1, 5))
S.append(P("Curve 1 makes soft playing count most, 3 is straight, 6 needs hard playing; MAX plays every note at "
           "full velocity. VOICES sets the polyphony (10, as on the MASK1). The page also shows the audio load, "
           "whether the second processor core is helping, and how many voices are allowed right now: if the "
           "audio runs long, MASQUE lets one voice go and allows it back after ten quiet seconds."))
S.append(P("<b>MIDI</b>: OMNI (every channel), one channel, or MPE (chapter 8). <b>USB</b> HOST: the FM-1's USB port "
           "plays host to a USB-MIDI controller through a powered hub or adapter (the FM-1 gives the port no power); "
           "meanwhile no computer is connected over USB. The page shows MPE's zones and the USB host's state."))
S.append(P("TUNING", h2))
S.append(P("Press GLO again for the tuning, which is the whole part's (KIST's tunings)."))
S.append(knobs([["1", "A HZ 300–500", "1/100 .00–.99", "SYSTEM", "ROOT C–B, or STEPS 5–72"]]))
S.append(Spacer(1, 5))
S.append(table([["SYSTEM", "What"],
                ["EQUAL", "any number of equal steps to the octave, 5 to 72 (12: equal temperament; 19, 31, 53 …)"],
                ["WERCKM. III", "Werckmeister III: four fifths a quarter of the Pythagorean comma narrow"],
                ["MEANTONE", "quarter-comma meantone: pure major thirds, a wolf fifth"],
                ["KIRNB. III", "Kirnberger III"],
                ["VALLOTTI", "six fifths a sixth of the Pythagorean comma narrow"]],
               [28 * mm, W - 28 * mm]))
S.append(Spacer(1, 4))
S.append(P("A stays exact at the reference. Every key is a step of the tuning: in 31-EDO the FM-1's keys play "
           "31sts of an octave, and OCT− / OCT+ move a whole octave of 31. SEMI, the glides, the bends and the pitch "
           "envelope count in steps; the arpeggiator's octaves, fifths and chords land on the steps nearest them. "
           "TUNE stays in cents, and the filter's TRK and the comb follow the real pitch. The dial shows the steps "
           "round the octave; a temperament's notes are drawn with their offsets five times over."))

# ------------------------------------------------------------------------------------------------ modes
S.append(PageBreak())
S.append(P("5  Play modes", h1))
S.append(P("Like the MASK1, MASQUE listens to <i>how</i> you play. Set the mode on the GENERAL page (MODE)."))
S.append(table([["Mode", "Behaviour"],
                ["POLY", "Polyphonic. A key played again picks up its envelopes where they are; notes glide from "
                         "the last one played (with PORT)."],
                ["MONO", "One voice, the last key wins. Legato playing does not restart the envelopes; always "
                         "glides."],
                ["SLUR", "Polyphonic, but a key one or two semitones from a held key, played legato, slides that "
                         "voice to the new note without a new attack. For a wider slur, run quickly through the "
                         "keys in between."],
                ["MONO PORTA", "One voice; glides only when played legato."],
                ["PARA", "Paraphonic: one set of envelopes for all voices, started by the first key and released "
                         "with the last."],
                ["MOLY", "Polyphonic when played staccato; legato notes glide from the previous one."],
                ["PARA SLUR", "PARA, plus SLUR's slides."],
                ["MONO RETRIG", "One voice; every key restarts the envelopes; glides when legato."],
                ["POLY RETRIG", "Polyphonic; every key starts its envelopes from silence."]],
               [30 * mm, W - 30 * mm]))
S.append(P("Tip: SLUR and MOLY reward a legato touch. Play a melody connected and it slides; lift between notes "
           "and each one speaks with its own attack.", note))

# ----------------------------------------------------------------------------------------------- looper
S.append(P("6  The looper", h1))
S.append(P("Open its page with <b>SEQ</b> (the transport works from any page)."))
S.append(table([["", ""],
                ["1. Arm", "Press REC. It blinks: the looper waits for a note."],
                ["2. Record", "Play. The first note starts the recording (REC lit)."],
                ["3. Close the loop", "Press REC again (or PLAY). That moment is the loop's length; playback "
                                     "starts at once (PLAY green)."],
                ["4. Play / stop", "PLAY stops and restarts the loop. You can play along over it."],
                ["5. New take", "REC while a loop is kept starts a new recording, replacing it."]][1:],
               [32 * mm, W - 32 * mm], head=False))
S.append(Spacer(1, 4))
S.append(P("It records notes, pitch bend, mod wheel, aftertouch and sustain, up to 4096 events. Notes still held "
           "when the loop closes are let go at its end."))

# ------------------------------------------------------------------------------------------------ sounds
S.append(P("7  Sounds: choosing, editing, writing", h1))
S.append(P("Turn <b>PRESETS</b> to browse: P00–P39 are the factory sounds, U000–U127 your own. The sound loads "
           "when the knob rests for a quarter of a second. Editing any value lights <b>SAVE</b> and marks the "
           "name with *. Choosing another sound discards unsaved edits."))
S.append(P("WRITE", h2))
S.append(knobs([["1", "SLOT U000–U127", "CURSOR 1–10", "CHAR", "ACTION"]]))
S.append(Spacer(1, 5))
S.append(P("Press SAVE to open WRITE. The page shows what is in the chosen slot, and the sound's name with a "
           "cursor: K2 moves the cursor, K3 changes the letter. K4 picks the action; SAVE does it:"))
S.append(table([["Action", "What"],
                ["WRITE", "Store the sound in the slot. It becomes the current sound and is restored at power-on."],
                ["INIT", "Start a new sound: a plain sine and nothing else."],
                ["REVERT", "Undo every edit: the sound as it was loaded."],
                ["SETUP", "Keep the GLOBAL settings and the sound as it is now for the next power-on."]],
               [24 * mm, W - 24 * mm]))
S.append(P("Writing to flash stops the sound for a moment: write between pieces.", small))
S.append(P("Factory sounds", h2))
names = ["MASQUE", "GLASS BELL", "MARIMBA", "SAW SQUARE", "VAPOUR", "BUZZ LEAD", "PULSE PAD", "MASK SWEEP",
         "SUPER SQ", "DRAWBARS", "TINE KEYS", "SQUARE ARP", "FORMANT", "VOX", "BRASS", "STRINGS", "KARPLUS",
         "KICK HAT", "CLAP ECHO", "WIND", "SUB BASS", "ACID", "DRONE", "BIT PAD", "RING BELL", "KOTO", "HARP",
         "SOFT SAW", "WARM POLY", "FLUTE", "CLAV", "BLEEPS", "DEEP SPACE", "TAPE KEYS", "ZAPPER", "SLUR LEAD",
         "MOLY PAD", "PARA STRGS", "FOLD BASS", "PHASE PAD"]
rows = [["P%02d %s" % (r + 10 * c, names[r + 10 * c]) for c in range(4)] for r in range(10)]
S.append(table(rows, [W / 4] * 4, head=False))
S.append(Spacer(1, 4))
S.append(P("Good places to start: P00 MASQUE (bouncing mask envelopes), P01 GLASS BELL and P02 MARIMBA (masks 29 "
           "and 30), P05 BUZZ LEAD (mask 78), P07 MASK SWEEP (a looping mask envelope), P12 FORMANT (REPEAT "
           "masks), P16 KARPLUS (the comb), P21 ACID (DRIVE), P35–P37 (the SLUR, MOLY and PARA modes).", small))

# ------------------------------------------------------------------------------------------------ midi
S.append(PageBreak())
S.append(P("8  MPE and MIDI", h1))
S.append(P("MPE", h2))
S.append(P("With MIDI set to MPE on the GLOBAL page, MASQUE follows MPE (MMA/AMEI RP-053) rule by rule, as OPUS XIV "
           "does: every note on its own channel, with its own bend, pressure and timbre."))
S.append(table([["Channel", "Message", "Means"],
                ["Member", "pitch bend", "the note's own bend (±48 semitones, or what RPN 0 sets for every Member)"],
                ["Member", "channel pressure", "the note's PRESS"],
                ["Member", "CC74", "the note's TIMBRE"],
                ["Master", "pitch bend", "added to every note of the zone (±2)"],
                ["Master", "channel pressure", "AT for every note of the zone"],
                ["Master", "CC74", "TIMBRE for the zone's notes whose own channel sends none"],
                ["no zone", "bend, pressure, CC74", "an ordinary channel: its notes' bend (the sound's BEND), AT, TIMBRE"],
                ["any", "poly pressure", "AT for that key's note"]],
               [22 * mm, 34 * mm, W - 56 * mm]))
S.append(Spacer(1, 4))
S.append(P("Until a controller sends an MPE Configuration Message, the lower zone has its Master Channel on 1 and "
           "fifteen Members (2–16); an upper zone (Master on 16) works too, and the newest zone wins where they "
           "would overlap. A configuration message stops the notes of the channels it moves. Values sent before a "
           "note start it. MPE+ (CC87) adds resolution to pressure and timbre. MPE notes are not slurred: their own "
           "bend slides them. The looper records the channels and their expression and plays them back as played."))
S.append(P("<b>The Osmose</b>: put it in MPE mode, then either its MIDI out into the FM-1's TRS jack (a DIN-to-TRS "
           "adapter: check which TRS wiring the FM-1 expects), or its USB into a powered hub on the FM-1's USB port "
           "with GLOBAL &gt; USB on HOST. DIN carries about a thousand messages a second; with many notes held and "
           "pressed it fills and the expression grows coarser, which the matrix's SMOOTH evens out.", note))
S.append(P("MIDI", h2))
S.append(table([["", ""],
                ["In", "USB and the 3.5 mm TRS jack, on the GLOBAL channel (or OMNI)."],
                ["Notes", "note on / off with velocity"],
                ["Aftertouch", "poly (per key) and channel; in MPE, as above"],
                ["Controllers", "pitch bend; CC 1 mod wheel; CC 64 sustain; CC 120 and 123 all off"],
                ["Program change", "0–39 the factory sounds, 40–127 U000–U087"],
                ["Clock", "clock, start, stop: the arpeggiator follows"],
                ["Sound values", "every value of the sound on its own CC (the map below)"],
                ["Out (USB)", "the FM-1's keys and their pressure, and each value as you turn it, as its CC, so a "
                              "computer can record your edits and play them back"]][1:],
               [32 * mm, W - 32 * mm], head=False))
S.append(P("The FM-1 has no pitch or mod wheel: those come only from an external controller over MIDI.", small))
S.append(P("The CC map", h2))
S.append(P("In MPE the map listens on the Master Channels (and channels in no zone), and CC74 is the notes' "
           "timbre there, not CUTF.", small))
S.append(P("A CC's 0–127 covers the value's whole range (a choice such as MODE or DEST in equal bands). MASK is "
           "14-bit: CC 20 / 21 carry its top seven bits and CC 52 / 53 the two below, so a controller that sends "
           "only CC 20 moves it four masks at a time and one that sends both reaches every mask."))
S.append(table([["Section", "CC: value"],
                ["OSC 1", "20 MASK (52 its low bits), 14 SEMI, 15 TUNE, 16 START, 17 SPD, 18 LOOP, 19 LEVEL"],
                ["OSC 2", "21 MASK (53 its low bits), 22 SEMI, 23 TUNE, 24 START, 25 SPD, 26 LOOP, 27 LEVEL"],
                ["OSC 1 ENV", "34 A, 35 D, 36 S, 37 R, 116 LOOP"],
                ["OSC 2 ENV", "39 A, 40 D, 41 S, 42 R, 43 LOOP"],
                ["NOISE", "76 FREQ, 77 TRACK, 78 LEVEL; ENV 44 A, 45 D, 46 S, 47 R, 48 LOOP"],
                ["FILTER", "74 CUTF, 71 RESO, 28 MODE, 29 TRK, 30 EXTRA, 31 ENV, 75 DRIVE; "
                           "ENV 49 A, 50 D, 51 S, 114 R, 115 LOOP"],
                ["PITCH", "79 START, 80 SPD, 81 END"],
                ["LFO 1", "82 AMT, 83 SPD, 84 DEST, 85 WAVE, 86 DLAY, 87 DCAY"],
                ["LFO 2", "108 AMT, 109 SPD, 110 DEST, 111 WAVE, 112 DLAY, 113 DCAY"],
                ["MATRIX", "the rows' amounts: 102–107 rows 1–6, 117 row 7, 118 row 8"],
                ["GENERAL", "3 MODE, 7 VOL, 12 SEMI, 5 PORT, 8 PANS, 9 RATE, 13 BEND"],
                ["FX", "94 1TYP, 93 1BAL, 95 2TYP, 91 2BAL"],
                ["ARP", "88 TYPE, 89 SPD, 90 HOLD, 92 TRIG"],
                ["The part", "1 mod wheel, 64 sustain, 120 and 123 all off"]],
               [26 * mm, W - 26 * mm]))
S.append(P("CC values change the sound being edited, as the knobs do: SAVE lights, and WRITE keeps them.", small))

# ------------------------------------------------------------------------------------------------ install
S.append(PageBreak())
S.append(P("9  Installing", h1))
S.append(P("Download <b>masque-X.Y.fwsc</b> from github.com/78Hz/masque (Releases, or the firmware artifact of "
           "the latest Build run). Connect the FM-1 directly to the computer with a USB data cable, switched on "
           "as normal; no update mode is needed."))
S.append(P("Before the first install, take a whole-flash backup (FM-1 Manager → Whole flash) while M-VAVE's "
           "firmware is still on it.", note))
S.append(table([["", ""],
                ["FM-1 Manager", "Open it, add the file under Firmware, press <i>Install on FM-1</i>. It also backs "
                                 "up and restores your sounds (Saved data) and puts M-VAVE's firmware back."],
                ["Command line", "<font face='Mono' size='8'>python3 fm1-base/tools/fm1_install.py "
                                 "masque-X.Y.fwsc</font> (needs <font face='Mono' size='8'>pip install mido "
                                 "python-rtmidi</font>)."],
                ["Going back", "Install M-VAVE's FM-1.fwsc the same way, or with M-VAVE's M-UPGRADE. An FM-1 that "
                               "will not start shows SAFE MODE after two tries, where USB still works."]][1:],
               [32 * mm, W - 32 * mm], head=False))

S.append(P("10  How MASQUE differs from the MASK1", h1))
for t in ["One part: the MASK1's multitimbral parts, layers and splits are not included.",
          "No wheels on the FM-1: pitch bend and modulation come over MIDI; the keys' pressure is aftertouch.",
          "The bitmask mapping is a reconstruction from published behaviour, not Kodamo's algorithm; individual "
          "masks other than those Kodamo names will not match the MASK1's.",
          "The 40 factory sounds are MASQUE's own.",
          "The filter goes further than the MASK1's: DRIVE, a PEAK mode and a tuned comb; MASQUE has its own CC map.",
          "A modulation matrix in place of the MASK1's three routings, MPE, and alternate tunings.",
          "The effect families and numbering follow the MASK1; the effects themselves are MASQUE's own designs, "
          "the reverb a plate.",
          "The looper keeps 4096 events (the MASK1 10,000)."]:
    S.append(P("•  " + t, ParagraphStyle("bl", parent=body, leftIndent=10, firstLineIndent=-8, spaceAfter=3)))
S.append(Spacer(1, 8))
S.append(P("MASQUE is open source under the Mozilla Public License 2.0, built on fm1-base and felucca-bsp. Its "
           "screen font is Barlow (SIL Open Font License 1.1). Kodamo and MASK1 are Kodamo's names; MASQUE is not "
           "affiliated with Kodamo.", small))

doc = Doc(os.path.join(OUT, "MASQUE-manual.pdf"))
doc.multiBuild(S)
print("manual  " + os.path.join(OUT, "MASQUE-manual.pdf"))
