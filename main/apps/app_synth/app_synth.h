/*
 * SPDX-FileCopyrightText: 2026 Phillip
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <hal/hal.h>
#include <cstdint>
#include <array>
#include <vector>

/**
 * @brief Synth - play mode + beat sequencer
 *
 * Play mode: Z X C V B N M play a C-major scale. 1-4 pick the waveform
 * (sine/square/saw/triangle), [ ] shift octave, - + adjust volume.
 * Notes are real generated waveform buffers via Speaker.playRaw(), looped
 * with a high repeat count and cut off with Speaker.stop() on key-up - not
 * M5Unified's built-in tone(), which only offers one fixed waveform. Each
 * channel double-buffers its waveform data (per the Speaker.playRaw() docs'
 * own warning about runtime-generated buffers) so a new note never frees
 * memory the speaker task might still be reading from the previous one.
 *
 * Beat mode: a 16-step, 3-track (kick/snare/hihat) step sequencer, with 4
 * pattern banks (1-4) you can flip between live - even while playing, for
 * a manually-performed arrangement. Arrow keys move the edit cursor, Enter
 * toggles the step under the cursor, Space starts/stops playback, - +
 * adjust tempo. Drum hits are synthesized (pitch-swept sine for kick,
 * noise+tone for snare, short noise burst for hihat), no audio assets
 * needed. S saves all 4 banks + tempo to the SD card, L loads them back.
 *
 * Tab switches between the Play and Beat screens, but the sequencer keeps
 * running in the background regardless of which screen is showing, so you
 * can play a melody over the beat.
 */
class AppSynth : public mooncake::AppAbility {
public:
    AppSynth();
    ~AppSynth();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Mode { Play, Beat };
    enum class Waveform { Sine, Square, Saw, Triangle };

    struct NoteKey {
        KeScanCode_t keyCode;
        const char* name;
        float freq;  // at octave offset 0
    };
    static const std::array<NoteKey, 7> NOTES;
    static constexpr int kSampleRate    = 44100;
    static constexpr int kNumTracks     = 3;
    static constexpr int kNumSteps      = 16;
    static constexpr int kNumBanks      = 4;
    static constexpr int kPlayChannels  = 3;  // matches the keyboard's own 3-key rollover
    static constexpr int kKickChannel   = 3;
    static constexpr int kSnareChannel  = 4;
    static constexpr int kHihatChannel  = 5;
    static constexpr const char* kSaveFilePath = "/sdcard/synth_patterns.dat";

    Mode _mode = Mode::Play;
    int _handle_key_event_slot_id = -1;
    std::string _status_message;
    uint32_t _status_message_until_ms = 0;

    /* ------------------------------- Play mode ------------------------------ */
    Waveform _waveform      = Waveform::Sine;
    int _octave_offset      = 0;
    uint8_t _volume         = 180;
    int _next_play_channel  = 0;
    bool _held[7]             = {false, false, false, false, false, false, false};
    int _channel_for_note[7]  = {-1, -1, -1, -1, -1, -1, -1};
    // Two buffers per channel, alternated on every note-on, so a new note
    // never overwrites memory the speaker task might still be reading.
    std::vector<int16_t> _note_buffers[kPlayChannels][2];
    int _note_buffer_slot[kPlayChannels] = {0, 0, 0};

    std::vector<int16_t> generate_waveform(Waveform type, float freq, int numCycles = 8);
    void note_on(int noteIndex);
    void note_off(int noteIndex);
    void stop_all_notes();
    void render_play();

    /* ------------------------------- Beat mode ------------------------------ */
    // [bank][track][step]
    bool _pattern[kNumBanks][kNumTracks][kNumSteps] = {};
    int _active_bank          = 0;
    int _cursor_track          = 0;
    int _cursor_step            = 0;
    int _play_step                = 0;
    bool _playing                  = false;
    uint32_t _bpm                  = 120;
    uint32_t _last_step_time_ms    = 0;

    std::vector<int16_t> _kick_buf;
    std::vector<int16_t> _snare_buf;
    std::vector<int16_t> _hihat_buf;

    void build_drum_sounds();
    void trigger_track(int track);
    void advance_sequencer();
    void render_beat();
    void set_status(const char* msg, uint32_t durationMs = 1500);

    bool save_patterns();
    bool load_patterns();

    void render();
    void handle_key_event(const Keyboard::KeyEvent_t& event);
};
