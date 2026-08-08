# CardSynth

A little synthesizer for the M5Stack Cardputer-Adv, built as one extra app on
top of M5Stack's own official [Cardputer-Adv demo firmware](https://github.com/m5stack/M5Cardputer-UserDemo)
(`CardputerADV` branch). Everything else - the launcher, display driver,
keyboard driver, SD card, etc. - is the stock demo; CardSynth just adds a
`Synth` app (`main/apps/app_synth/`) alongside its existing apps
(Scan, Record, Chat, Clock, GPS, ...).

## What it does

**Play mode** - a small playable keyboard:
- `Z X C V B N M` play a C-major scale (C4-B4), held for as long as the key is down
- `1 2 3 4` switch the waveform (sine / square / sawtooth / triangle)
- `[` `]` shift octave down/up
- `-` `+` volume down/up
- `Tab` switches to Beat mode

**Beat mode** - a 16-step, 3-track drum sequencer (kick/snare/hihat):
- Arrow keys move the step cursor
- `Enter` toggles the step under the cursor on/off
- `Space` starts/stops playback
- `-` `+` tempo down/up
- `1 2 3 4` switch between 4 pattern banks (A/B/C/D) - works *live*, even while
  playing, so you can flip banks to perform a rough arrangement by hand
- `S` saves all 4 banks + tempo to the SD card, `L` loads them back
- `Tab` switches back to Play mode

The sequencer keeps running in the background even when you switch to Play
mode, so you can play a melody over your beat instead of the two being
mutually exclusive.

## How it works

- Built as a `mooncake::AppAbility`, the same app pattern the rest of the
  demo uses - registered in `main/apps/apps.h` and installed in
  `main/main.cpp`.
- No audio samples anywhere. Play-mode notes generate a few cycles of the
  selected waveform as a 16-bit PCM buffer and loop it via
  `M5.Speaker.playRaw()` until the key is released. Each channel
  double-buffers its waveform (alternating on every note-on) so a new note
  never frees memory the speaker's background task might still be reading
  from the previous one.
- Drum hits are one-shot synthesized buffers: a pitch-swept sine (180Hz to
  50Hz) for the kick, a short tone blended with noise for the snare, and a
  fast-decaying noise burst for the hihat.
- Uses 6 of the speaker's 8 available channels - 3 for melody notes
  (matching the keyboard's own 3-key rollover limit) and one each for
  kick/snare/hihat - so drums and melody never cut each other off.
- Saved patterns live at `/sdcard/synth_patterns.dat`, a small binary file
  (magic number + BPM + all 4 banks x 3 tracks x 16 steps).
- The app canvas is smaller than the physical screen (the framework
  reserves the rest for its own status bar and a sidebar showing held
  modifier keys), so both screens lay out against `canvas.width()`/
  `height()` at runtime rather than assuming full-screen pixel positions.

## Building

This is an ESP-IDF project (not Arduino/PlatformIO), targeting v5.4.2.

```bash
python3 ./fetch_repos.py    # fetches M5GFX, M5Unified, mooncake, etc.
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash
```

Note: on Windows, build from a path with no spaces - some of the fetched
components' CMakeLists.txt break on spaces in the project path.

## Hardware

Built and tested on the M5Stack Cardputer-Adv (Stamp-S3A, ESP32-S3, 8MB
flash, no PSRAM).

## Credits

Built on [m5stack/M5Cardputer-UserDemo](https://github.com/m5stack/M5Cardputer-UserDemo)
(`CardputerADV` branch), MIT licensed. See that project's README for the
full list of libraries it's built on (M5Unified, M5GFX, mooncake, RadioLib,
and others).

---

License: MIT, same as the upstream project.
