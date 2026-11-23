# MASQUE: design decisions

The design as built, and why. Changes go here first.

**Platform.** fm1-base on felucca-bsp, as KIST: open (MPL-2.0), no closed code, no radio (the voices want the RAM).

**The oscillator.** Kodamo has not published how the MASK1's mask bits map to its operations, only what they do (a
sine cut into sections that are repeated, scaled, inverted or silenced; 9 bits; sub-masks in the LSBs) and a handful of
masks by number (31 sine, 7 saw, 8 a square of small saws, 20..25 square-like, 56 pulse, 78 a buzz at the sine's
pitch, 28..30 bells, 16k - 1 rounded). MASQUE's mapping (README, `dsp/bitmask.h`) was chosen because every one of
those comes out of it and because it is a bit mask in the plain sense: an AND over the phase. The operations are keyed
to the dropped bits so that they are not the same as toggling a KEEP bit (an INVERT keyed to the top bit alone would
just rename bit 4): 457 of the 512 masks differ (up to their sign).

**Aliasing.** Four times the rate in the oscillators, then two halfbands (7 taps, then 15): a mask's edges are sharp
anywhere in its cycle, so band-limiting each one (polyBLEP) would need its edges found per mask; oversampling needs
nothing per mask. The MASK1's own oscillators are oversampled (8x on the MASK1EX).

**Voices.** Ten, as the MASK1 (up to twelve on the GLOBAL page); the second core renders every other one. Each voice
counts its own control steps (5.5 kHz): envelopes, masks, pitches, the filter's coefficient (gliding within the step).
A voice is kept until its filter stops ringing, so a resonant release never clicks off.

**Expression.** Velocity and the keys' pressure (poly aftertouch) from the FM-1's keys; the MASK1's play modes
(slur detection at one or two semitones, paraphony, moly). The FM-1 has no wheels: bend and modulation over MIDI.

**Effects.** The MASK1's families and numbering; KIST-P's plate for the reverbs; a 16-bit floating-point delay line
(0.74 s) so the delay fits beside the plate, the looper and the screen in the pool.

**Storage.** The setup (the part's settings and the sound as it was left) and four banks of 32 user sounds, each a
store object; all of them backed up over USB together.

**Panel.** Sections on the buttons as the MASK1 groups them (SEL the four voice sections, ENV their envelopes, EDIT the
rest of the voice); the four knobs under the screen as a row of values; ALGORITHM the mask, the one number.
