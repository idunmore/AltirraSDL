# Audio Output

## Current Architecture (Windows)

```
POKEY Emulator (synthesis at ~64 kHz)
    |
    v
IATSyncAudioSource       -- clean interface (WriteAudio callback)
    |
    v
IATAudioMixer            -- mixes multiple sources, resamples to output rate
    |
    v
IATAudioOutput           -- CLEAN high-level interface
    |  (manages mixer, volume, latency, API selection)
    v
IVDAudioOutput           -- CONTAMINATED (Init takes tWAVEFORMATEX)
    |
    v
[WaveOut | DirectSound | XAudio2 | WASAPI]
```

Two interfaces exist at different levels:

- **`IATAudioOutput`** (in `at/ataudio/audiooutput.h`): Clean. No Win32
  types. Methods: `Init(ATScheduler&)`, `InitNativeAudio()`, `SetApi()`,
  `WriteAudio()`, `AsMixer()`, `SetVolume()`, `SetLatency()`, etc.

- **`IVDAudioOutput`** (in `vd2/Riza/audioout.h`): Contaminated. `Init()`
  takes `tWAVEFORMATEX*`. Used internally by `IATAudioOutput` implementations.

The SDL3 build works at the `IATAudioOutput` level, completely replacing the
lower layers.

## SDL3 Architecture

```
POKEY Emulator (synthesis at ~64 kHz)
    |
    v
IATSyncAudioSource
    |
    v
IATAudioMixer (unchanged)
    |
    v
ATAudioOutputSDL3 : IATAudioOutput
    |
    v
SDL_AudioStream → SDL_AudioDevice
    |
    v
OS Audio (PulseAudio/ALSA/CoreAudio/WASAPI)
```

### ATAudioOutputSDL3 Class

```cpp
class ATAudioOutputSDL3 final : public IATAudioOutput {
public:
    ~ATAudioOutputSDL3();

    // IATAudioOutput (full interface)
    void Init(ATScheduler& scheduler) override;
    void InitNativeAudio() override;
    // ... all 23 pure virtual methods implemented ...
    void WriteAudio(const float *left, const float *right,
                    uint32 count, bool pushAudio,
                    bool pushStereoAsAudio, uint64 timestamp) override;

private:
    SDL_AudioStream *mpStream = nullptr;  // owns device + stream
    ATAudioMixerStub mMixer;              // stub (no sync source mixing yet)
    ATAudioSamplePool mPool;
    IATAudioTap *mpAudioTap = nullptr;
    float mVolume = 1.0f;
    float mMixLevels[kATAudioMixCount] {};
    int mLatencyMs = 40;
    int mExtraBufferMs = 0;
    int mMixingRate = 63920;   // cps / 28, updated by SetCyclesPerSecond
    bool mMuted = false;
    bool mPaused = false;
};
```

### Initialization

```cpp
void ATAudioOutputSDL3::InitNativeAudio() {
    SDL_AudioSpec spec {};
    spec.freq = mMixingRate;       // POKEY rate (~63920 Hz NTSC)
    spec.format = SDL_AUDIO_F32;   // float samples (matches WriteAudio)
    spec.channels = 2;             // stereo

    // SDL_OpenAudioDeviceStream creates device + stream + binding in one call.
    // SDL3 handles resampling from POKEY rate to the device's native rate.
    mpStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

    // The stream starts paused; resume immediately.
    SDL_ResumeAudioStreamDevice(mpStream);
}
```

Called from `main_sdl3.cpp` after `ATLoadSettings()` so that audio
configuration (latency, volume) is already applied.

### Audio Flow

1. The scheduler ticks the POKEY emulator, which generates samples at ~64 kHz
   (machine clock / 28 cycles per sample).

2. POKEY calls `IATAudioOutput::WriteAudio()` with separate left/right float
   buffers at the POKEY mixing rate (~63,920 Hz NTSC, ~63,337 Hz PAL).

3. `ATAudioOutputSDL3::WriteAudio()` interleaves L/R, applies volume, and
   pushes to the SDL3 audio stream:

```cpp
void ATAudioOutputSDL3::WriteAudio(const float *left, const float *right,
    uint32 count, ...) {
    const float vol = mMuted ? 0.0f : mVolume;

    // Interleave left/right into stereo, apply volume
    float interleaved[kChunkSize * 2];
    for (uint32 i = 0; i < count; ++i) {
        interleaved[i*2]   = left[i] * vol;
        interleaved[i*2+1] = (right ? right[i] : left[i]) * vol;
    }

    SDL_PutAudioStreamData(mpStream, interleaved, count * 2 * sizeof(float));
}
```

4. SDL3 resamples from the POKEY rate to the hardware output rate and delivers
   to the OS audio subsystem on its audio thread.

**Note on the mixer:** The Windows `ATAudioOutput` class implements both
`IATAudioOutput` and `IATAudioMixer`, mixing additional sync audio sources
(disk sounds, speaker clicks, cassette motor) into the output alongside
POKEY. The SDL3 build currently uses a stub mixer that only passes POKEY
audio through. Disk/speaker/cassette sound effects are not yet mixed in.

### Dynamic Rate Changes

When the video standard changes (NTSC ↔ PAL), `SetCyclesPerSecond()` is
called with the new machine clock rate. The SDL3 implementation updates the
stream's input format via `SDL_SetAudioStreamFormat()`, so SDL3 adjusts
its resampling ratio automatically.

### Buffer Management

SDL3 manages its own audio buffering internally. The implementation monitors
`SDL_GetAudioStreamQueued()` to track underflow and overflow events, reported
through `GetAudioStatus()`.

### SetApi

On Windows, the emulator lets users choose between WaveOut, DirectSound,
XAudio2, and WASAPI. On SDL3, the audio backend is chosen automatically by
SDL3 based on the platform. The `SetApi()` method is a no-op.

### Sample Rate

The POKEY synthesizes at the machine clock rate (~1.79 MHz / 28 = ~63,920
Hz for NTSC). Unlike the Windows implementation (which resamples internally
to 44100-48000 Hz before sending to the OS), the SDL3 implementation passes
the native POKEY rate to SDL3 and lets SDL3's audio stream handle the
resampling to the hardware output rate.

### Factory Function

`ATCreateAudioOutput()` is called inside `ATSimulator::Init()` (in
`simulator.cpp` line ~920), not by the frontend. The factory lives in
`ATAudio/source/audiooutput.cpp` and currently returns `new ATAudioOutput`
(the Win32 implementation).

For the SDL3 build, provide an alternative `audiooutput_sdl3.cpp` that
implements the same factory:

```cpp
// ATAudio/source/audiooutput_sdl3.cpp
IATAudioOutput *ATCreateAudioOutput() {
    return new ATAudioOutputSDL3();
}
```

The build system compiles either `audiooutput.cpp` (Windows) or
`audiooutput_sdl3.cpp` (SDL3), never both. This keeps the existing
`ATAudioOutput` class completely untouched and avoids `#ifdef` blocks in
the original code.

Note: `audiooutput.cpp` also contains `ATAudioOutput` (the Win32
implementation class, ~1100 lines). The SDL3 file contains
`ATAudioOutputSDL3` plus the factory function. Both files define the same
external symbol (`ATCreateAudioOutput`) so they are mutually exclusive at
link time.

### ATAudioApi Enum

The existing enum defines five values:
- `kATAudioApi_WaveOut`, `kATAudioApi_DirectSound`, `kATAudioApi_XAudio2`,
  `kATAudioApi_WASAPI`, `kATAudioApi_Auto`

On SDL3, `SetApi()` is a no-op (SDL3 chooses the best backend automatically).
`GetApi()` returns `kATAudioApi_Auto`.

## Summary of New Files

| File | Purpose |
|------|---------|
| `src/ATAudio/source/audiooutput_sdl3.cpp` | `ATAudioOutputSDL3` implementing `IATAudioOutput` |

## Interface Dependency

Depends only on:

- `IATAudioOutput` (clean)
- `IATSyncAudioSource` / `IATAudioMixer` (clean)
- SDL3 audio headers

Does **not** depend on `Riza/audioout.h`, `tWAVEFORMATEX`, DirectSound,
WASAPI, or any Win32 headers.
