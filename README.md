# Simpler Sampler

A small Raspberry Pi sampler prototype built with C++ and SDL2. The UI is
designed around an 800x480 display and uses a 400x240 internal render target so
the pixel-art look stays crisp on small screens.

## Controls

- `1` - `8`: trigger a pad
- `Tab`: switch between the two eight-pad pages
- MIDI note input: trigger pads using the TR-style drum map below
- `B`: open the sample browser
- `E`: open the editor for the selected pad
- `M`: open audio-card and MIDI-input settings
- `G`: toggle CRT graphics effects on/off; effects start disabled for lower CPU/GPU usage
- Editor `Tab`: select the START or END trim handle
- Editor `Left` / `Right`: move the selected trim handle
- Editor `Ctrl` + arrows: fine 1 ms steps
- Editor `Shift` + arrows: coarse 100 ms steps
- Editor `Z`: snap both trim handles to nearby zero crossings
- Editor `[`: prepend silence, `]`: append silence
- Editor `Ctrl` + silence key: 10 ms, normal: 100 ms, `Shift`: 500 ms
- Editor `U`: undo the last destructive edit (up to 8 steps)
- Editor `Space`: preview/play the current selected sample without applying an edit
- Press `E` in the editor to open the Echo parameter popup
- Press `L` in the editor to open the Flanger parameter popup
- Press `C` in the editor to open the Compressor parameter popup
- Press `O` in the editor to open the Distortion parameter popup
- Press `R` in the editor to open the Reverse parameter popup
- Press `Y` in the editor to open the Time Stretch parameter popup
- Echo popup time: `;` / `'` decrease/increase delay in milliseconds
- Echo popup power: `,` / `.` decrease/increase feedback
- Hold `Ctrl` for fine changes, or `Shift` for coarse changes
- Echo popup `Space`: preview Echo and play it, keeping the popup open; repeated previews do not stack
- Echo popup `Enter`: apply Echo and close the popup
- Echo popup `Escape`: cancel without applying
- Flanger popup parameters:
  - `A` / `Z`: decrease/increase delay
  - `S` / `X`: decrease/increase modulation depth
  - `D` / `C`: decrease/increase modulation rate
  - `F` / `V`: decrease/increase feedback
  - `Ctrl`: fine changes, `Shift`: coarse changes
  - `Space`: preview without stacking, `Enter`: apply and close, `Escape`: cancel
- Compressor popup parameters:
  - `Q` / `A`: threshold down/up
  - `W` / `S`: ratio down/up
  - `E` / `D`: makeup gain down/up
  - `Ctrl`: fine changes, `Shift`: coarse changes
  - `Space`: preview, `Enter`: apply and close, `Escape`: cancel
- Distortion popup parameters:
  - `A` / `Z`: drive down/up
  - `S` / `X`: mix down/up
  - `Ctrl`: fine changes, `Shift`: coarse changes
  - `Space`: preview, `Enter`: apply and close, `Escape`: cancel
- Fade In and Fade Out popups:
  - `I`: open Fade In, `V`: open Fade Out
  - `A` / `Z`: decrease/increase duration
  - `Ctrl`: 1 ms steps, `Shift`: 100 ms steps
  - `Space`: preview, `Enter`: apply and close, `Escape`: cancel
- Reverse popup parameters:
  - `A` / `Z`: decrease/increase reverse blend
  - `Ctrl`: fine changes, `Shift`: coarse changes
  - `Space`: preview, `Enter`: apply and close, `Escape`: cancel
- Time Stretch popup parameters:
  - `A` / `Z`: decrease/increase stretch ratio
  - `Ctrl`: fine changes, `Shift`: coarse changes
  - `Space`: preview, `Enter`: apply and close, `Escape`: cancel
- Editor `+` / `-`: adjust gain or pitch amount
- Editor `T`: trim, `N`: normalize, `I`: fade in, `V`: fade out, `R`: reverse
- Editor `E`: echo, `L`: flanger, `C`: compressor, `O`: distortion
- Editor `P`: pitch/resample, `U`: undo, `Enter`: close editor
- `Up` / `Down`: browse files when the browser is open
- `Enter`: load the highlighted file into the selected pad
- `Left` / `Right`: change the selected pad
- `Up` / `Down`: change the selected pad's pitch
- `Escape`: close the browser, or quit when it is closed
- Mouse/touch: press a pad directly

Each of the 16 pads starts with the original built-in generated sound, so the
sampler works without any files. Samples can then be loaded into either
eight-pad page from normal audio files. The in-app browser uses
`~/simpler/` and creates that folder automatically if it does not exist. Put
files named `1.wav` through `16.wav` there, or
pass up to sixteen paths on the command line:

```sh
./build/simpler_sampler kick.wav snare.ogg hat.mp3 clap.wav
```

WAV is always supported; OGG, MP3, and FLAC are supported when the installed
SDL2_mixer build includes their codecs. Mono files are duplicated to both
outputs and stereo files preserve their left/right channels. Samples are converted to the mixer rate and mixed with up to 16 simultaneous
voices.

All internal samples use interleaved stereo buffers. Mono files are duplicated
to left and right during loading, and the editor/effects path validates the
stereo frame layout before processing so effects preserve both channels.

Graphics post-processing is isolated in `graphics_effects.cpp` and runs as an
Graphics post-processing uses a portable SDL-only path for Raspberry Pi and
Orange Pi compatibility. It applies inexpensive CRT scanlines, a slow
intermittent phosphor sweep, and a restrained vignette without mixing raw
OpenGL calls into SDL's accelerated renderer.

The editor operates on the selected pad's in-memory sample. Effects are
destructive for the current session; reloading the application restores the
default or file-loaded version.

## Build on Raspberry Pi OS

```sh
sudo apt install build-essential cmake pkg-config libasound2-dev libsdl2-dev libsdl2-mixer-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/simpler_sampler
```

### MIDI note map

The MIDI input uses ALSA Sequencer. Connect a controller or DAW to the
application's `MIDI In` port with `aconnect`. Pad notes are:

| Pad | MIDI note | TR-style instrument |
|---:|---:|---|
| 1 | 36 | Bass drum |
| 2 | 38 | Snare drum |
| 3 | 43 | Low tom |
| 4 | 47 | Mid tom |
| 5 | 50 | High tom |
| 6 | 37 | Rim shot |
| 7 | 39 | Clap |
| 8 | 42 | Closed hi-hat |
| 9 | 46 | Open hi-hat |
| 10 | 49 | Crash cymbal |
| 11 | 51 | Ride cymbal |
| 12 | 44 | Pedal hi-hat |
| 13 | 41 | Low floor tom |
| 14 | 45 | Low tom 2 |
| 15 | 48 | Hi-mid tom |
| 16 | 52 | China/extra cymbal |

The first eleven notes follow the common Roland TR drum layout; the remaining
five continue with standard percussion notes so all 16 pads have unique
triggers. MIDI note-on velocity is currently used only as a trigger gate.

To find the application's ALSA port:

```sh
aconnect -l
aconnect <controller-client>:<port> <simpler-client>:<port>
```

The `M` settings screen lists SDL audio output devices and ALSA MIDI input
ports. Use `Tab` to switch between the two lists, `Up`/`Down` to select a
device, and `Enter` to apply. Changing the audio device briefly restarts the
audio stream; choosing a MIDI port subscribes the sampler to that ALSA source.

## Build an AppImage

The repository includes a `linuxdeploy` packaging script. It builds a
Release executable, stages the application metadata, and bundles the SDL2 and
SDL2_mixer shared-library dependencies discovered from the executable. The
AppImage must be built on the target architecture; build it on Raspberry Pi
for ARM64/ARMHF deployment, or on an Orange Pi for its native architecture.

Install the build prerequisites, including `curl`:

```sh
sudo apt install build-essential cmake pkg-config curl libasound2-dev \
    libsdl2-dev libsdl2-mixer-dev
chmod +x packaging/build-appimage.sh
packaging/build-appimage.sh
```

The first run downloads the matching `linuxdeploy` binary. The resulting file
is placed in `dist/`. Run it with:

```sh
./dist/Simpler-Sampler-*.AppImage
```

To use a pre-downloaded linuxdeploy binary:

```sh
LINUXDEPLOY=/path/to/linuxdeploy packaging/build-appimage.sh
```

For a kiosk-like 800x480 launch, use SDL's KMSDRM or framebuffer video driver
as appropriate for the image. Do not force `MESA_LOADER_DRIVER_OVERRIDE=zink`
unless the Vulkan device satisfies Zink's feature requirements; native Mesa
rendering is preferred on Raspberry Pi:

```sh
SDL_VIDEODRIVER=kmsdrm ./build/simpler_sampler
```
