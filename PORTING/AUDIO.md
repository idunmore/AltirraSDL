# Audio Output

## Current Architecture (Windows)

```
POKEY Emulator (synthesis at ~64 kHz)
    |
    v
IATSyncAudioSource       -- clean interface (WriteAudio callback)
    |
    v
ATAudioOutput : IATAudioOutput + IATAudioMixer
    |  (mixes sync sources, edges, filters, resamples)
    |
    v
IVDAudioOutput           -- CONTAMINATED (Init takes tWAVEFORMATEX)
    |
    v
[WaveOut | DirectSound | XAudio2 | WASAPI]
```

Pipeline inside `ATAudioOutput::InternalWriteAudio()`:

```
POKEY L/R → copy to source buffer at kPreFilterOffset
         → sync sources mix additively (disk, printer, cassette sounds)
         → PreFilterDiff (differencing stage of DC removal)
         → RenderEdges (speaker clicks, covox, cassette edges)
         → edge sample player (convolved speaker click sounds)
         → PreFilterEdges (integration + DC high-pass removal)
         → Filter (FIR low-pass at ~15 kHz)
         → async sources mix at output rate (when no audio tap)
         → polyphase FIR resampling ~64 kHz → ~48 kHz sint16
         → write to IVDAudioOutput
```

Two interfaces exist at different levels:

- **`IATAudioOutput`** (in `at/ataudio/audiooutput.h`): Clean. No Win32
  types. Methods: `Init(ATScheduler&)`, `InitNativeAudio()`, `SetApi()`,
  `WriteAudio()`, `AsMixer()`, `SetVolume()`, `SetLatency()`, etc.

- **`IVDAudioOutput`** (in `vd2/Riza/audioout.h`): Contaminated. `Init()`
  takes `tWAVEFORMATEX*`. Used internally by `ATAudioOutput`.

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
ATAudioOutputSDL3 : IATAudioOutput + IATAudioMixer
    |  (full mixing pipeline, identical to Windows up to the output stage)
    |
    v
SDL_AudioStream (float32 stereo at ~64 kHz)
    |
    v
SDL3 internal resampling → OS Audio (PulseAudio/ALSA/CoreAudio)
```

Pipeline inside `ATAudioOutputSDL3::InternalWriteAudio()`:

```
POKEY L/R → copy to source buffer at kPreFilterOffset
         → sync sources mix additively (disk, printer, cassette sounds)
         → PreFilterDiff (differencing stage of DC removal)
         → RenderEdges (speaker clicks, covox, cassette edges)
         → edge sample player (convolved speaker click sounds)
         → PreFilterEdges (integration + DC high-pass removal)
         → Filter (FIR low-pass at ~15 kHz)
         → async sources mix at mixing rate (~64 kHz)
         → audio tap forwarding (for recording)
         → interleave L/R float → SDL_PutAudioStreamData
         → SDL3 resamples to device rate
```

### Key difference from Windows

The SDL3 version skips the internal polyphase resampler. Instead of
resampling ~64 kHz → ~48 kHz sint16 and feeding to `IVDAudioOutput`, it
pushes float32 stereo at the native POKEY rate to `SDL_AudioStream`, which
handles resampling to the hardware device rate.

This simplifies the output stage while keeping the entire mixing and
filtering pipeline identical to Windows.

### ATAudioOutputSDL3 Class

```cpp
class ATAudioOutputSDL3 final : public IATAudioOutput, public IATAudioMixer {
public:
    // IATAudioOutput — all 23 methods implemented
    void Init(ATScheduler& scheduler) override;
    void InitNativeAudio() override;
    void WriteAudio(...) override;
    IATAudioMixer& AsMixer() override { return *this; }
    // Volume applied through ATAudioFilter::SetScale (matches Windows)
    float GetVolume() override { return mFilters[0].GetScale(); }
    void SetVolume(float vol) override {
        mFilters[0].SetScale(vol);
        mFilters[1].SetScale(vol);
    }
    // ...

    // IATAudioMixer — all 12 methods implemented
    void AddSyncAudioSource(IATSyncAudioSource*) override;
    void RemoveSyncAudioSource(IATSyncAudioSource*) override;
    IATSyncAudioSamplePlayer& GetSamplePlayer() override;     // real ATAudioSamplePlayer
    IATSyncAudioSamplePlayer& GetEdgeSamplePlayer() override;  // real ATAudioSamplePlayer
    IATSyncAudioEdgePlayer& GetEdgePlayer() override;           // real ATSyncAudioEdgePlayer
    IATSyncAudioSamplePlayer& GetAsyncSamplePlayer() override;  // real ATAudioSamplePlayer
    // ...

private:
    SDL_AudioStream* mpStream = nullptr;

    // Full mixer components (same as Windows ATAudioOutput)
    vdautoptr<ATAudioSamplePool> mpSamplePool;
    vdautoptr<ATAudioSamplePlayer> mpSamplePlayer;
    vdautoptr<ATAudioSamplePlayer> mpEdgeSamplePlayer;
    vdautoptr<ATAudioSamplePlayer> mpAsyncSamplePlayer;
    vdautoptr<ATSyncAudioEdgePlayer> mpEdgePlayer;

    ATAudioFilter mFilters[2];          // DC removal + low-pass
    float mPrevDCLevels[2] {};

    alignas(16) float mSourceBuffer[2][kSourceBufferSize] {};
    alignas(16) float mMonoMixBuffer[kBufferSize] {};
    // ...
};
```

### Mixer components

| Component | Class | Purpose |
|-----------|-------|---------|
| Sample player | `ATAudioSamplePlayer` | Disk rotation, track step, printer sounds |
| Edge sample player | `ATAudioSamplePlayer` | Speaker click via convolution player |
| Edge player | `ATSyncAudioEdgePlayer` | Speaker/covox/cassette edge transitions |
| Async sample player | `ATAudioSamplePlayer` | Async sounds at mixing rate |
| Audio filters | `ATAudioFilter[2]` | DC removal high-pass + FIR low-pass at ~15 kHz |
| Sample pool | `ATAudioSamplePool` | Stock sample storage (11 embedded PCM files) |

### Stock audio samples

The 11 PCM audio samples (disk spin, track steps, speaker click, modem
relay, printer sounds) are embedded as C arrays in
`src/AltirraSDL/romdata/audio_samples.h`, generated by
`generate_audio_samples.py` from the PCM files in `src/Altirra/res/`.

`ATLoadMiscResource()` in `oshelper_stubs.cpp` returns the embedded data
for the matching resource IDs. `ATAudioRegisterStockSamples()` (called
from `simulator.cpp` during init) loads them into the sample pool.

### Initialization sequence

```
simulator.cpp:922  ATCreateAudioOutput()         → creates ATAudioOutputSDL3
simulator.cpp:923  mpAudioOutput->Init(scheduler) → creates sample players, edge player
simulator.cpp:925  ATAudioRegisterStockSamples()  → loads 11 PCM samples into pool
simulator.cpp:927  RegisterService<IATAudioMixer> → makes mixer available to devices
simulator.cpp:929  mPokey.Init(... mpAudioOutput) → POKEY gets edge sample player
simulator.cpp:950  mpCassette->Init(... AsMixer)  → cassette gets mixer
simulator.cpp:966  mpDiskDrives[i]->Init(... GetSamplePlayer) → disk drives get sample player
main_sdl3.cpp:551  ATLoadSettings()              → sets volume, mute, latency, mix levels
main_sdl3.cpp:570  InitNativeAudio()             → creates SDL3 audio stream
```

### Buffer layout

The source buffer layout matches Windows exactly:

```
Position:  0          kFilterOffset(16)    kPreFilterOffset(48)    48+count
           |          |                    |                        |
           [overlap]  [filter overlap]     [new data written here]  [edge tail]
           (from prev frame)               (POKEY + sync sources)
```

- `kBufferSize = 1536` — max samples per InternalWriteAudio call
- `kFilterOffset = 16` — FIR filter reads from here
- `kPreFilterOffset = 48` — new data placed here (kFilterOffset + kFilterOverlap*2)
- `kSourceBufferSize = 1600` — total buffer size including overlap + edge tail

After output, the buffer is shifted: `kPreFilterOffset` samples from the
end are moved to position 0 as overlap for the next frame. Unlike Windows
(where the resampler may not consume everything), SDL3 always outputs all
samples and resets `mBufferLevel = 0`.

### Dynamic rate changes

When the video standard changes (NTSC ↔ PAL), `SetCyclesPerSecond()` is
called with the new machine clock rate. The SDL3 implementation:
1. Updates `mMixingRate` and `mMixingRateInt`
2. Calls `SDL_SetAudioStreamFormat()` to update the stream's input rate
3. Updates all three sample player rates via `SetRates()`

### Turbo mode / overflow protection

When the emulation runs faster than real time (turbo/warp mode), audio
blocks are dropped if the SDL3 queue exceeds a threshold based on
`mLatency + mExtraBuffer + 50ms`. This prevents unbounded latency growth.

### Async source mixing

Windows has two async mixing paths:
- **With audio tap:** async sources mix at ~64 kHz into the filtered buffer
- **Without audio tap:** async sources mix at ~48 kHz during resampling

SDL3 always uses the first path (mixing at ~64 kHz) since there is no
internal resampler. The async sample player is configured with POKEY rate
via `SetRates()`.

### Volume

Volume is applied through `ATAudioFilter::SetScale()`, matching Windows.
The filter's scale factor is baked into the output during the low-pass
filter stage. Muting zeroes the interleaved output in the SDL3 push stage.

### SetApi

On Windows, the emulator lets users choose between WaveOut, DirectSound,
XAudio2, and WASAPI. On SDL3, the audio backend is chosen automatically by
SDL3 based on the platform. The `SetApi()` method is a no-op.

### Filters

`SetFiltersEnabled()` calls `ATAudioFilter::SetActiveMode()` on both
channels, controlling the DC removal high-pass time constant. The FIR
low-pass always runs. The default is active mode (fast DC removal).

## Summary of files

| File | Purpose |
|------|---------|
| `src/ATAudio/source/audiooutput_sdl3.cpp` | `ATAudioOutputSDL3` + `ATSyncAudioEdgePlayer` + factory |
| `src/AltirraSDL/stubs/oshelper_stubs.cpp` | `ATLoadMiscResource` returns embedded PCM audio |
| `src/AltirraSDL/romdata/audio_samples.h` | 11 embedded PCM samples as C arrays |
| `src/AltirraSDL/romdata/generate_audio_samples.py` | Generator script for audio_samples.h |

## Interface dependency

Depends only on:

- `IATAudioOutput` (clean)
- `IATAudioMixer` / `IATSyncAudioSource` / `IATSyncAudioEdgePlayer` (clean)
- `ATAudioFilter` / `ATAudioSamplePlayer` / `ATAudioConvolutionPlayer` (from ATAudio library)
- `ATAudioSamplePool` (from ATAudio library)
- SDL3 audio headers

Does **not** depend on `Riza/audioout.h`, `tWAVEFORMATEX`, DirectSound,
WASAPI, or any Win32 headers.
