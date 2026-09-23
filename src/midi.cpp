#include "midi.h"

#include <alsa/asoundlib.h>

#include <array>
#include <cstdio>
#include <chrono>
#include <thread>

namespace {
// Roland TR-style drum notes first, followed by the remaining GM percussion
// notes so all 16 sampler pads have a stable MIDI assignment.
constexpr std::array<int, kPadCount> kPadNotes{
    36, 38, 43, 47, 50, 37, 39, 42,
    46, 49, 51, 44, 41, 45, 48, 52
};
}

MidiInput::MidiInput(AppState& state) : state_(state) {}

MidiInput::~MidiInput() {
    stop();
}

bool MidiInput::start() {
    if (running_.load()) {
        return true;
    }
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
        std::fprintf(stderr, "MIDI disabled: ALSA sequencer unavailable\n");
        return false;
    }
    snd_seq_set_client_name(seq, "Simpler Sampler");
    const int port = snd_seq_create_simple_port(seq, "MIDI In",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) {
        std::fprintf(stderr, "MIDI disabled: cannot create ALSA input port\n");
        snd_seq_close(seq);
        return false;
    }
    sequencer_ = seq;
    running_.store(true);
    thread_ = std::thread(&MidiInput::run, this);
    std::fprintf(stderr, "MIDI input ready: connect with aconnect to the Simpler Sampler port\n");
    return true;
}

void MidiInput::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (sequencer_ != nullptr) {
        snd_seq_close(sequencer_);
        sequencer_ = nullptr;
    }
}

void MidiInput::run() {
    auto* seq = sequencer_;
    while (running_.load() && state_.running.load()) {
        snd_seq_event_t* event = nullptr;
        if (snd_seq_event_input(seq, &event) >= 0 && event != nullptr
            && event->type == SND_SEQ_EVENT_NOTEON && event->data.note.velocity > 0) {
            for (int pad = 0; pad < kPadCount; ++pad) {
                if (kPadNotes[pad] == event->data.note.note) {
                    state_.midiTriggers.fetch_or(1u << pad);
                    break;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
