/*
 * SPDX-FileCopyrightText: 2026 Phillip
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_synth.h"
#include <apps/utils/theme.h>
#include <apps/utils/audio/audio.h>
#include <mooncake_log.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

using namespace mooncake;

const std::array<AppSynth::NoteKey, 7> AppSynth::NOTES = {{
    {KEY_Z, "C4", 261.63f},
    {KEY_X, "D4", 293.66f},
    {KEY_C, "E4", 329.63f},
    {KEY_V, "F4", 349.23f},
    {KEY_B, "G4", 392.00f},
    {KEY_N, "A4", 440.00f},
    {KEY_M, "B4", 493.88f},
}};

AppSynth::AppSynth()
{
    setAppInfo().name = "Synth";
}

AppSynth::~AppSynth() {}

void AppSynth::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // The demo's global keyboard click sound resets the master volume on
    // every keypress, stomping our own note volume - disable it while open.
    audio::set_keyboard_sfx_enable(false);
    GetHAL().speaker.setVolume(_volume);
    build_drum_sounds();
    render();

    _handle_key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& event) { handle_key_event(event); });
}

void AppSynth::onRunning()
{
    // Keep the sequencer running regardless of which screen (Play/Beat) is
    // showing, so a melody can be played live over the beat.
    if (_playing) {
        advance_sequencer();
    }

    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppSynth::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_handle_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_handle_key_event_slot_id);
    }
    stop_all_notes();
    audio::set_keyboard_sfx_enable(true);
}

/* ---------------------------------------------------------------------- */
/*                                Input                                   */
/* ---------------------------------------------------------------------- */

void AppSynth::handle_key_event(const Keyboard::KeyEvent_t& event)
{
    if (event.isModifier) {
        return;
    }

    if (event.state && event.keyCode == KEY_TAB) {
        _mode = (_mode == Mode::Play) ? Mode::Beat : Mode::Play;
        render();
        return;
    }

    if (_mode == Mode::Play) {
        if (event.state) {
            switch (event.keyCode) {
                case KEY_1: _waveform = Waveform::Sine; render(); return;
                case KEY_2: _waveform = Waveform::Square; render(); return;
                case KEY_3: _waveform = Waveform::Saw; render(); return;
                case KEY_4: _waveform = Waveform::Triangle; render(); return;
                case KEY_LEFTBRACE:
                    if (_octave_offset > -2) _octave_offset--;
                    render();
                    return;
                case KEY_RIGHTBRACE:
                    if (_octave_offset < 2) _octave_offset++;
                    render();
                    return;
                case KEY_MINUS:
                    _volume = (_volume > 20) ? _volume - 20 : 0;
                    GetHAL().speaker.setVolume(_volume);
                    render();
                    return;
                case KEY_EQUAL:
                    _volume = (_volume < 235) ? _volume + 20 : 255;
                    GetHAL().speaker.setVolume(_volume);
                    render();
                    return;
                default:
                    break;
            }
        }

        for (size_t i = 0; i < NOTES.size(); i++) {
            if (NOTES[i].keyCode != event.keyCode) {
                continue;
            }
            if (event.state && !_held[i]) {
                note_on(static_cast<int>(i));
            } else if (!event.state && _held[i]) {
                note_off(static_cast<int>(i));
            }
            render();
            break;
        }
        return;
    }

    // Beat mode - only react to key-down for sequencer controls.
    if (!event.state) {
        return;
    }

    switch (event.keyCode) {
        case KEY_LEFT: _cursor_step = (_cursor_step + kNumSteps - 1) % kNumSteps; break;
        case KEY_RIGHT: _cursor_step = (_cursor_step + 1) % kNumSteps; break;
        case KEY_UP: _cursor_track = (_cursor_track + kNumTracks - 1) % kNumTracks; break;
        case KEY_DOWN: _cursor_track = (_cursor_track + 1) % kNumTracks; break;
        case KEY_ENTER:
            _pattern[_active_bank][_cursor_track][_cursor_step] =
                !_pattern[_active_bank][_cursor_track][_cursor_step];
            break;
        case KEY_SPACE:
            _playing = !_playing;
            if (_playing) {
                _play_step         = 0;
                _last_step_time_ms = GetHAL().millis();
            }
            break;
        case KEY_MINUS:
            if (_bpm > 60) _bpm -= 5;
            break;
        case KEY_EQUAL:
            if (_bpm < 240) _bpm += 5;
            break;
        // Bank switch works live, even mid-playback, so you can perform an
        // arrangement by hand instead of programming a fixed song chain.
        case KEY_1: _active_bank = 0; break;
        case KEY_2: _active_bank = 1; break;
        case KEY_3: _active_bank = 2; break;
        case KEY_4: _active_bank = 3; break;
        case KEY_S: save_patterns(); break;
        case KEY_L: load_patterns(); break;
        default:
            return;
    }
    render();
}

/* ---------------------------------------------------------------------- */
/*                               Play mode                                */
/* ---------------------------------------------------------------------- */

std::vector<int16_t> AppSynth::generate_waveform(Waveform type, float freq, int numCycles)
{
    if (freq <= 0.0f) freq = 1.0f;
    int samplesPerCycle = static_cast<int>(kSampleRate / freq + 0.5f);
    if (samplesPerCycle < 4) samplesPerCycle = 4;
    int totalSamples = samplesPerCycle * numCycles;

    std::vector<int16_t> buf(totalSamples);
    const float amplitude = 32767.0f * 0.6f;

    for (int i = 0; i < totalSamples; i++) {
        float phase  = static_cast<float>(i % samplesPerCycle) / samplesPerCycle;  // 0..1
        float sample = 0.0f;
        switch (type) {
            case Waveform::Sine: sample = sinf(2.0f * static_cast<float>(M_PI) * phase); break;
            case Waveform::Square: sample = (phase < 0.5f) ? 1.0f : -1.0f; break;
            case Waveform::Saw: sample = 2.0f * phase - 1.0f; break;
            case Waveform::Triangle:
                sample = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
                break;
        }
        buf[i] = static_cast<int16_t>(sample * amplitude);
    }
    return buf;
}

void AppSynth::note_on(int noteIndex)
{
    float freq = NOTES[noteIndex].freq * powf(2.0f, static_cast<float>(_octave_offset));

    int ch                 = _next_play_channel;
    _next_play_channel     = (_next_play_channel + 1) % kPlayChannels;
    _channel_for_note[noteIndex] = ch;
    _held[noteIndex]              = true;

    // Alternate buffer slot for this channel - the previous buffer stays
    // valid (not freed/reassigned) until the *next* note on this same
    // channel, giving the speaker's background task time to stop reading
    // it. Reusing a single buffer in place raced with playback and crashed
    // when a second note started before the first had fully stopped.
    int slot                 = _note_buffer_slot[ch];
    _note_buffer_slot[ch]    = 1 - slot;
    _note_buffers[ch][slot]  = generate_waveform(_waveform, freq);

    GetHAL().speaker.playRaw(
        _note_buffers[ch][slot].data(), _note_buffers[ch][slot].size(), kSampleRate, false, /*repeat*/ 1000000, ch,
        /*stop_current_sound*/ true
    );
}

void AppSynth::note_off(int noteIndex)
{
    if (_channel_for_note[noteIndex] >= 0) {
        GetHAL().speaker.stop(_channel_for_note[noteIndex]);
        _channel_for_note[noteIndex] = -1;
    }
    _held[noteIndex] = false;
}

void AppSynth::stop_all_notes()
{
    for (size_t i = 0; i < NOTES.size(); i++) {
        if (_channel_for_note[i] >= 0) {
            GetHAL().speaker.stop(_channel_for_note[i]);
            _channel_for_note[i] = -1;
        }
        _held[i] = false;
    }
}

void AppSynth::render_play()
{
    auto& canvas = GetHAL().canvas;
    // The app canvas is smaller than the physical screen (the framework
    // reserves the rest for its own status bar and held-modifier-key
    // sidebar) - always lay out against the canvas's own size, not
    // hardcoded screen pixels, or content overflows into that reserved
    // area and gets hidden behind it.
    const int W = canvas.width();
    const int H = canvas.height();
    canvas.fillScreen(THEME_COLOR_BG);

    canvas.setTextSize(1);
    canvas.setCursor(0, 0);
    canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    canvas.print("PLAY ");
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    static const char* waveNames[] = {"Sine", "Sqr", "Saw", "Tri"};
    canvas.printf(
        "%s O:%+d V:%d%%\n", waveNames[static_cast<int>(_waveform)], _octave_offset, (_volume * 100) / 255
    );
    int headerLines = 1;
    if (_playing) {
        canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        canvas.printf("Beat: bank %c\n", static_cast<char>('A' + _active_bank));
        headerLines = 2;
    }

    const int footerH = 10;
    const int keyTop  = 10 * (headerLines + 1);
    const int keyH    = H - keyTop - footerH;
    const int keyW    = W / static_cast<int>(NOTES.size());
    for (size_t i = 0; i < NOTES.size(); i++) {
        int x = static_cast<int>(i) * keyW;
        if (_held[i]) {
            canvas.fillRect(x, keyTop, keyW - 1, keyH, TFT_GREEN);
            canvas.setTextColor(TFT_BLACK, TFT_GREEN);
        } else {
            canvas.drawRect(x, keyTop, keyW - 1, keyH, TFT_WHITE);
            canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        }
        canvas.setCursor(x + 2, keyTop + keyH - 10);
        canvas.print(NOTES[i].name);
    }

    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas.setCursor(0, H - footerH);
    canvas.print("1-4 [] -+ Tab");

    GetHAL().pushCanvas();
}

/* ---------------------------------------------------------------------- */
/*                               Beat mode                                */
/* ---------------------------------------------------------------------- */

void AppSynth::build_drum_sounds()
{
    // Kick: pitch sweep 180Hz -> 50Hz with a fast amplitude decay.
    {
        const int totalSamples = static_cast<int>(kSampleRate * 0.15f);
        _kick_buf.resize(totalSamples);
        float phase = 0.0f;
        for (int i = 0; i < totalSamples; i++) {
            float t    = static_cast<float>(i) / totalSamples;
            float freq = 180.0f + (50.0f - 180.0f) * t;
            phase += 2.0f * static_cast<float>(M_PI) * freq / kSampleRate;
            float env    = powf(1.0f - t, 3.0f);
            _kick_buf[i] = static_cast<int16_t>(sinf(phase) * 32767.0f * 0.8f * env);
        }
    }

    // Snare: noise + a short low tone for body.
    {
        const int totalSamples = static_cast<int>(kSampleRate * 0.12f);
        _snare_buf.resize(totalSamples);
        float phase = 0.0f;
        for (int i = 0; i < totalSamples; i++) {
            float t = static_cast<float>(i) / totalSamples;
            float env = powf(1.0f - t, 4.0f);
            phase += 2.0f * static_cast<float>(M_PI) * 200.0f / kSampleRate;
            float tone    = sinf(phase) * 0.3f;
            float noise   = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
            _snare_buf[i] = static_cast<int16_t>((tone + noise * 0.7f) * 32767.0f * 0.8f * env);
        }
    }

    // Hihat: short noise burst.
    {
        const int totalSamples = static_cast<int>(kSampleRate * 0.05f);
        _hihat_buf.resize(totalSamples);
        for (int i = 0; i < totalSamples; i++) {
            float t       = static_cast<float>(i) / totalSamples;
            float env     = powf(1.0f - t, 6.0f);
            float noise   = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
            _hihat_buf[i] = static_cast<int16_t>(noise * 32767.0f * 0.6f * env);
        }
    }
}

void AppSynth::trigger_track(int track)
{
    switch (track) {
        case 0:
            GetHAL().speaker.playRaw(_kick_buf.data(), _kick_buf.size(), kSampleRate, false, 1, kKickChannel, true);
            break;
        case 1:
            GetHAL().speaker.playRaw(
                _snare_buf.data(), _snare_buf.size(), kSampleRate, false, 1, kSnareChannel, true
            );
            break;
        case 2:
            GetHAL().speaker.playRaw(
                _hihat_buf.data(), _hihat_buf.size(), kSampleRate, false, 1, kHihatChannel, true
            );
            break;
        default:
            break;
    }
}

void AppSynth::advance_sequencer()
{
    uint32_t now             = GetHAL().millis();
    uint32_t stepIntervalMs  = 60000 / _bpm / 4;  // 16th notes
    if (now - _last_step_time_ms < stepIntervalMs) {
        return;
    }

    _last_step_time_ms = now;
    for (int track = 0; track < kNumTracks; track++) {
        if (_pattern[_active_bank][track][_play_step]) {
            trigger_track(track);
        }
    }
    _play_step = (_play_step + 1) % kNumSteps;
    render();
}

void AppSynth::set_status(const char* msg, uint32_t durationMs)
{
    _status_message           = msg;
    _status_message_until_ms  = GetHAL().millis() + durationMs;
}

bool AppSynth::save_patterns()
{
    auto probe = GetHAL().sdCardProbe();
    if (!probe.is_mounted) {
        set_status("SD card not found");
        return false;
    }

    struct SaveData {
        uint32_t magic;
        uint32_t bpm;
        bool pattern[kNumBanks][kNumTracks][kNumSteps];
    } data;
    data.magic = 0x544e5953;  // "SYNT"
    data.bpm   = _bpm;
    memcpy(data.pattern, _pattern, sizeof(_pattern));

    FILE* fp = fopen(kSaveFilePath, "wb");
    if (!fp) {
        set_status("Save failed");
        return false;
    }
    fwrite(&data, sizeof(data), 1, fp);
    fclose(fp);
    set_status("Saved!");
    return true;
}

bool AppSynth::load_patterns()
{
    auto probe = GetHAL().sdCardProbe();
    if (!probe.is_mounted) {
        set_status("SD card not found");
        return false;
    }

    FILE* fp = fopen(kSaveFilePath, "rb");
    if (!fp) {
        set_status("No saved patterns");
        return false;
    }

    struct SaveData {
        uint32_t magic;
        uint32_t bpm;
        bool pattern[kNumBanks][kNumTracks][kNumSteps];
    } data;
    size_t got = fread(&data, sizeof(data), 1, fp);
    fclose(fp);

    if (got != 1 || data.magic != 0x544e5953) {
        set_status("Bad save file");
        return false;
    }

    _bpm = data.bpm;
    memcpy(_pattern, data.pattern, sizeof(_pattern));
    set_status("Loaded!");
    return true;
}

void AppSynth::render_beat()
{
    auto& canvas = GetHAL().canvas;
    // Lay out against the canvas's actual size (smaller than the physical
    // screen - see the comment in render_play()), not hardcoded pixels.
    const int W = canvas.width();
    const int H = canvas.height();
    canvas.fillScreen(THEME_COLOR_BG);

    canvas.setTextSize(1);
    canvas.setCursor(0, 0);
    canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    canvas.print("BEAT ");
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas.printf(
        "%c %lubpm %s\n", static_cast<char>('A' + _active_bank), static_cast<unsigned long>(_bpm),
        _playing ? "PLAY" : "STOP"
    );

    static const char* trackNames[kNumTracks] = {"K", "S", "H"};
    const int gridX   = 12;
    const int gridTop = 11;
    const int footerH = 20;  // two lines of hints/status
    const int gridH   = H - gridTop - footerH;
    const int cellH   = gridH / kNumTracks;
    const int cellW   = (W - gridX) / kNumSteps;

    for (int track = 0; track < kNumTracks; track++) {
        int y = gridTop + track * cellH;
        canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        canvas.setCursor(0, y + cellH / 2 - 4);
        canvas.print(trackNames[track]);

        for (int step = 0; step < kNumSteps; step++) {
            int x           = gridX + step * cellW;
            bool active     = _pattern[_active_bank][track][step];
            bool isCursor   = (track == _cursor_track && step == _cursor_step);
            bool isPlayhead = (step == _play_step && _playing);

            canvas.fillRect(x, y, cellW - 1, cellH - 1, active ? TFT_GREEN : THEME_COLOR_BG);
            canvas.drawRect(
                x, y, cellW - 1, cellH - 1, isCursor ? TFT_YELLOW : (isPlayhead ? TFT_ORANGE : TFT_DARKGREY)
            );
        }
    }

    int footerY = gridTop + gridH;
    if (GetHAL().millis() < _status_message_until_ms) {
        canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
        canvas.setCursor(0, footerY);
        canvas.print(_status_message.c_str());
    } else {
        canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        canvas.setCursor(0, footerY);
        canvas.print("1-4 bank Ent tog");
        canvas.setCursor(0, footerY + 10);
        canvas.print("Spc play -+ tmp");
    }

    GetHAL().pushCanvas();
}

/* ---------------------------------------------------------------------- */

void AppSynth::render()
{
    if (_mode == Mode::Play) {
        render_play();
    } else {
        render_beat();
    }
}
