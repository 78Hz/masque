# MASQUE — the player's manual

MASQUE turns the M-VAVE FM-1 into a bitmask synthesizer after Kodamo's MASK1. Every voice has two bitmask
oscillators and a noise, mixed into a resonant filter; four envelopes shape them, two LFOs and the keys move them.

## The screen

- **The top line**: the sound's number (P00..P39 the factory sounds, U000..U127 yours), its name, a `*` while it is
  edited and not written, and a dot for each voice (lit while it sounds).
- **The second line**: the page, or for a moment what you just changed. `REC` / `LOOP` / `ARP` at the right while the
  looper records or plays and while the arpeggiator runs.
- **The middle**: what the page is about: the masks' waveforms (they move as the mask envelope and the LFOs move
  them), an envelope, the filter's response, an LFO's wave, the effects.
- **The bottom**: the page's values, in rows of four, read K1..K4 left to right. The lit row is the one the knobs
  turn. SELECT moves to the next row (and on to the next page).

## The panel

| Control | What |
|---|---|
| HOME | the sound's page: K1 MASK 1, K2 MASK 2, K3 CUTF, K4 RESO |
| SEL | OSC 1 → OSC 2 → NOISE → FILTER |
| ENV | lit: the section's envelope instead of its settings (OSC 1, OSC 2, NOISE, FILTER) |
| LFO | LFO 1 ↔ LFO 2 |
| EDIT | PITCH → MOD → GENERAL |
| FX | the effects |
| ARP | the arpeggiator; pressed again on its page: HOLD on / off. Lit while it plays |
| SEQ | the looper's page |
| REC | the looper: waits for the first note (blinking), records (lit); again: plays the loop |
| PLAY | the loop plays (green) or stops |
| GLO | the part's settings (GLOBAL) |
| SAVE | lit while the sound is edited. Press: the WRITE page; press again: its action |
| SELECT | the next row, the next page |
| ALGORITHM | MASK of the oscillator shown (OSC 1 on the other pages). A quick turn goes four at a time |
| PRESETS | the next sound; it loads when the knob rests a quarter of a second |
| OCT- / OCT+ | the FM-1's keys an octave down / up (-3..+3); both together: 0 |
| MASTER | the volume |

## The masks

The mask (0..511) is the oscillator's whole character: one sine, cut into 32 sections and put back together by the
mask's bits. Some to start from:

| Mask | What |
|---|---|
| 31 | the sine |
| 15, 47, 63, 79, 95 ... (16k - 1) | rounded, mellow; 15 and 47 an octave up |
| 29, 30, 28 | the sine with fine steps: bells, marimbas, glassy keys |
| 23, 19 | saw-like at the sine's pitch |
| 7 | a saw, two octaves up |
| 8 | a square whose low half is small saws, an octave up |
| 20..25 | square-like |
| 24 | a square with saws on its floor |
| 40, 56 | pulses |
| 78 | a buzz at the sine's pitch |
| 0..6 | very bright, octaves up: 0 five octaves |
| 128..255 | silences in the wave: thinner, pulse-like |
| 256..511 | three turns of the sine in each piece: formants, voices, bleeps |

Masks below 16 (and some others) sound octaves above the note: SEMI brings them down (STRINGS plays mask 7 at -24).

## OSC 1, OSC 2

| Row | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| 1 | MASK 0..511 | SEMI -60..+60 | TUNE -99..+99 cents | LEVEL 0..99 (the envelope's L) |
| 2 | START -99..+99 | SPD 0..99 | LOOP NO / YES / BIDI | |

**The mask envelope** (START, SPD, LOOP): a key starts the mask START masks away from MASK and moves it to MASK at SPD
(1 slow .. 99 nearly at once; 0 off). LOOP YES starts again from START each time it arrives (a saw of masks), BIDI
goes back and forth (a triangle). It passes through the three sub-masks between each pair of masks.

**ENV** (each oscillator's own level envelope): L (level), A (attack, straight), D (decay, curved), S (sustain), R
(release, curved), LOOP (attack and decay again and again until the key is let go). A, D and R: 0 is at once, 30
about 20 ms, 50 about 0.1 s, 70 about 0.8 s, 99 about 12 s.

## NOISE

| K1 | K2 | K3 |
|---|---|---|
| FREQ 0..99 | TRACK 0..99 % | LEVEL 0..99 |

A random value held and taken again FREQ times a second: 99 is white noise, lower is crunchier, down to a few
values a second. TRACK makes higher keys take it faster. ENV: its envelope.

## FILTER

| Row | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| 1 | CUTF 0..127 | RESO 0..99 | MODE LP / HP / BP / NOTCH | TRK 0..99 % |
| 2 | EXTRA | ENV -99..+99 | DRIVE 0..99 | |

A two-pole (12 dB) state-variable filter, self-oscillating near the top of RESO. Its resonance saturates, so it
growls rather than clips; **DRIVE** pushes the sound into it harder (and the comb too). **MODE**: LP, HP, BP, NOTCH,
**PEAK** (the resonant band added to the sound, nothing cut), **COMB+** and **COMB-** (a delay line tuned to the cutoff
and fed back by RESO: a pitched resonator; fed noise or a short sound, a plucked string or a metal tube; COMB- the
hollow kind, an octave down). With TRK at 99 the comb plays the keys' pitches. 24DB is a second, clean stage after
the first: four poles, one peak. CUTF is in semitones (60 is middle
C's pitch, 127 about 12.5 kHz). TRK follows OSC 1's pitch. NOTCH is deepest with RESO at 0. **EXTRA**: 12DB (one
filter), 24DB (the filter twice), LP1..LP7 or HP1..HP7 (a fixed low or high pass after it: LP1 350 Hz .. LP7 9 kHz,
HP1 40 Hz .. HP7 1.6 kHz). **ENV** (row 2, and the envelope's L on the ENV page): how far the filter envelope moves
the cutoff, either way.

## PITCH

| K1 | K2 | K3 |
|---|---|---|
| START -48..+48 | SPD 0..40 | END -48..+48 |

The pitch envelope: a key starts START semitones away and glides to the note at SPD (1 very slow, 40 almost at
once; 0 off); let go, it glides to END at the same speed.

## LFO 1, LFO 2

| Row | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| 1 | AMT 0..99 | SPD 0..99 (0.03..30 Hz) | DEST | WAVE |
| 2 | DLAY 0..99 | DCAY 0..99 | | |

Free-running, for the whole part. **DEST**: FREQ, VOL, FILT, MASK, PAN, O1 FREQ, O1 VOL, O1 MASK, O2 FREQ, O2 VOL,
O2 MASK, N FREQ, N VOL, FX1 TYP, FX2 TYP, FX2 BAL. **WAVE**: SIN, SQR, UP, DOWN, SAWS (two small saws), STEP (four
stairs), HSIN (half sines), RAND (sample and hold). **DLAY**: the LFO waits this long after the first key, then fades
in (not while the mod wheel or aftertouch moves its amount or speed). **DCAY**: it fades out after this long (0: never).

## MOD

| Row | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| 1 | V-DEST | V-AMT -99..+99 | M-DEST | M-AMT -99..+99 |
| 2 | AT-DEST | AT-AMT -99..+99 | | |

Velocity (V): FLT, F EG (the filter envelope's amount), VOL, MASK, P EG (the pitch envelope's START), ATK (the
attacks). The mod wheel (M, MIDI CC 1) and aftertouch (AT: the FM-1's key pressure, or MIDI's): FLT, LFO1 AMT, LFO1
SPD, LFO2 AMT, LFO2 SPD, VOL, MASK, FREQ, O1 VOL, O1 MASK, O2 VOL, O2 MASK, N FREQ, N VOL, FX1 BAL, FX2 BAL.

## GENERAL

| Row | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| 1 | MODE | VOL 0..99 | SEMI -24..+24 | PORT 0..13 |
| 2 | PANS 0..99 | RATE 0..99 | BEND 0..12 | |

**PORT**: the glide (0 none, 13 very long); the play modes' glides need it above 0 (SLUR's slides always slide a little). **PANS**: how far the voices spread left and right. **RATE**: the
envelopes faster up the keyboard, slower down it. **BEND**: the pitch bend's range in semitones.

**The play modes** (MODE): they listen to how you play.

| Mode | What |
|---|---|
| POLY | polyphonic; a key played again picks up its envelopes where they are; glides from the last note |
| MONO | one voice, the last key; legato does not restart the envelopes; always glides |
| SLUR | polyphonic, but a key one or two semitones from a key held, played legato, slides that voice to it (no new attack); further apart: step through the keys between, quickly |
| MONO PORTA | one voice; glides only when played legato |
| PARA | paraphonic: one set of envelopes for every voice, started by the first key, let go with the last |
| MOLY | polyphonic when played staccato; legato notes glide from the one before |
| PARA SLUR | PARA, and SLUR's slides |
| MONO RETRIG | one voice; every key starts the envelopes again; glides when legato |
| POLY RETRIG | polyphonic; every key starts its envelopes from silence |

## FX

| K1 | K2 | K3 | K4 |
|---|---|---|---|
| 1TYP | 1BAL 0..100 | 2TYP | 2BAL 0..100 |

**FX1**: CH01..CH16 chorus (CH11..14 flangers, CH15..16 phasers), DT01..16 distortion (DT13..16 fold the wave),
BC01..16 bitcrusher (BC09..16 with their own slow sweep), RM01..16 ring modulator (RM01..04 a stereo tremolo).
**FX2**: DL01..16 delay (95 ms .. 0.7 s, the even ones ping-pong), RV01..16 reverb (a plate, small and short to
large and long), DT01..16 distortion. The balances: 0 dry, 100 the effect.

## ARP

| K1 | K2 | K3 | K4 |
|---|---|---|---|
| TYPE | SPD 20..400 BPM | HOLD | TRIG |

**TYPE**: NONE, 1OCT 2OCT 3OCT (up), DBL (each twice), ALT (lowest, highest, inwards), PONG PON2 PON3 (up and down),
RND RND2 RND3 (random), MIN MAJ SUS2 SUS4 7TH (a chord from each key) and their two-octave MIN2 MAJ2 SU22 SU42 7TH2,
1O 2O 3O (each key and its octaves), 5TH 5TH2 (each key and its fifth). Steps are sixteenths at SPD; with MIDI clock
coming in, they follow it. **HOLD**: the keys stay after you let go, until you play a new chord. **TRIG** off: one
note that changes pitch instead of a note each step.

## LOOPER (SEQ)

REC: the looper waits for a note (REC blinks); the first note starts the recording (REC lit). REC again (or PLAY)
ends it there: that is the loop's length, and it plays at once (PLAY green). PLAY stops and starts it. REC while a
loop is kept starts a new one. It records the notes, pitch bend, the mod wheel, aftertouch and sustain, up to 4096
events; you can play over it.

## GLOBAL

| Row | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| 1 | MIDI CH OMNI, 1..16 | A HZ 400.0..470.0 | VEL CRV 1..6, MAX | AT CRV 1..6 |
| 2 | VOICES 4..12 | OCTAVE -3..+3 | | |

The curves: 1 the softest touch counts most, 3 straight, 6 only hard playing counts; MAX: every note at full
velocity. VOICES: how many may sound (10, as the MASK1). The page also shows the render's load, whether the second
core helps, and the voices allowed now: when the render runs long, a voice is let go and allowed back after ten quiet
seconds.

## WRITE (SAVE)

| K1 | K2 | K3 | K4 |
|---|---|---|---|
| SLOT U000..U127 | CURSOR 1..10 | CHAR | ACTION |

The page shows what is in the slot now, and the name with its cursor. SAVE does the ACTION:
**WRITE** the sound into the slot (it becomes the sound chosen), **INIT** a new sound (a sine, nothing else),
**REVERT** the sound as it was loaded, **SETUP** keep the part's settings and the sound as it is now for the next
power-on (WRITE keeps them too). Writing stops the sound for a moment.

## MIDI

In (USB and the TRS jack, on the GLOBAL page's channel): notes, poly and channel aftertouch, pitch bend, CC 1 (mod
wheel), CC 64 (sustain), CC 120 and 123 (all off), program change (0..39 the factory sounds, 40..127 U000..U087),
clock, start and stop (the arpeggiator), and every value of the sound on the CC map below. Out (USB): the FM-1's
keys and their pressure, and each value as you turn it, as its CC (so a computer can record your moves).

### The CC map

A CC's 0..127 covers the value's whole range. MASK is 14-bit: CC 20 / 21 its top seven bits, CC 52 / 53 the two
below (a controller sending only CC 20 moves it four masks at a time).

| Section | CCs |
|---|---|
| OSC 1 | 20 MASK (52 its low bits), 14 SEMI, 15 TUNE, 16 START, 17 SPD, 18 LOOP, 19 LEVEL |
| OSC 2 | 21 MASK (53 its low bits), 22 SEMI, 23 TUNE, 24 START, 25 SPD, 26 LOOP, 27 LEVEL |
| OSC 1 ENV | 34 A, 35 D, 36 S, 37 R, 116 LOOP |
| OSC 2 ENV | 39 A, 40 D, 41 S, 42 R, 43 LOOP |
| NOISE | 76 FREQ, 77 TRACK, 78 LEVEL; ENV 44 A, 45 D, 46 S, 47 R, 48 LOOP |
| FILTER | 74 CUTF, 71 RESO, 28 MODE, 29 TRK, 30 EXTRA, 31 ENV, 75 DRIVE; ENV 49 A, 50 D, 51 S, 114 R, 115 LOOP |
| PITCH | 79 START, 80 SPD, 81 END |
| LFO 1 | 82 AMT, 83 SPD, 84 DEST, 85 WAVE, 86 DLAY, 87 DCAY |
| LFO 2 | 108 AMT, 109 SPD, 110 DEST, 111 WAVE, 112 DLAY, 113 DCAY |
| MOD | 102 V-DEST, 103 V-AMT, 104 M-DEST, 105 M-AMT, 106 AT-DEST, 107 AT-AMT |
| GENERAL | 3 MODE, 7 VOL, 12 SEMI, 5 PORT, 8 PANS, 9 RATE, 13 BEND |
| FX | 94 1TYP, 93 1BAL, 95 2TYP, 91 2BAL |
| ARP | 88 TYPE, 89 SPD, 90 HOLD, 92 TRIG |
| (the part) | 1 mod wheel, 64 sustain, 120 / 123 all off |

## The factory sounds

| | | | |
|---|---|---|---|
| P00 MASQUE | P10 TINE KEYS | P20 SUB BASS | P30 CLAV |
| P01 GLASS BELL | P11 SQUARE ARP | P21 ACID | P31 BLEEPS |
| P02 MARIMBA | P12 FORMANT | P22 DRONE | P32 DEEP SPACE |
| P03 SAW SQUARE | P13 VOX | P23 BIT PAD | P33 TAPE KEYS |
| P04 VAPOUR | P14 BRASS | P24 RING BELL | P34 ZAPPER |
| P05 BUZZ LEAD | P15 STRINGS | P25 KOTO | P35 SLUR LEAD |
| P06 PULSE PAD | P16 KARPLUS | P26 HARP | P36 MOLY PAD |
| P07 MASK SWEEP | P17 KICK HAT | P27 SOFT SAW | P37 PARA STRGS |
| P08 SUPER SQ | P18 CLAP ECHO | P28 WARM POLY | P38 FOLD BASS |
| P09 DRAWBARS | P19 WIND | P29 FLUTE | P39 PHASE PAD |
