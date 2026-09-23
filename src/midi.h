#pragma once

#include "editor.h"
#include <alsa/asoundlib.h>

#include <atomic>
#include <thread>
#include <vector>

struct MidiPortInfo {
    int client = -1;
    int port = -1;
    std::string name;
};

class MidiInput {
public:
    explicit MidiInput(AppState& state);
    ~MidiInput();
    bool start();
    void stop();
    static std::vector<MidiPortInfo> listInputPorts();
    bool connect(const MidiPortInfo& port);
    bool connectIndex(size_t index);

private:
    void run();
    AppState& state_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    snd_seq_t* sequencer_ = nullptr;
    std::vector<MidiPortInfo> ports_;
};
