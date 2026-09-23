#pragma once

#include "editor.h"
#include <alsa/asoundlib.h>

#include <atomic>
#include <thread>

class MidiInput {
public:
    explicit MidiInput(AppState& state);
    ~MidiInput();
    bool start();
    void stop();

private:
    void run();
    AppState& state_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    snd_seq_t* sequencer_ = nullptr;
};
