# Verification: Audio Mixer Internals (InternalWriteAudio)

Line-by-line analysis of Windows `ATAudioOutput::InternalWriteAudio()` (audiooutput.cpp:638-1025)
and comparison with SDL3 `ATAudioOutputSDL3::InternalWriteAudio()` (audiooutput_sdl3.cpp:593-885).

---

## SECTION A: Outer WriteAudio loop (Win 601-636, SDL3 545-577)

**What it does:**
POKEY calls `WriteAudio()` once per frame with ~1050-1271 samples. The outer loop
splits this into chunks of at most `kBufferSize` (1536) samples and calls
`InternalWriteAudio()` for each chunk.

**Key detail:** When `mBufferLevel == kBufferSize` (buffer full), `tc=0` and
`InternalWriteAudio(count=0)` is called. This is intentional in Windows — it
runs the resampling/output section to drain the buffer even when no new audio
is added. Then `if (!tc) break` exits.

**Windows:**
```
mWritePosition += count;   // total samples for profiling
for(;;) {
    tc = kBufferSize - mBufferLevel;   // room in buffer
    if (tc > count) tc = count;
    InternalWriteAudio(left, right, tc, ...);
    if (!tc) break;           // buffer was full, tried to drain
    count -= tc;
    if (!count) break;        // all consumed
    timestamp += 28 * tc;     // advance time
    left += tc; if(right) right += tc;
}
```

**SDL3:** Identical structure.

**SDL3 difference:** In SDL3, mBufferLevel is always 0 at the start of each call
(we flush everything). So tc=0 never occurs. The loop always runs exactly once.
This means the `if (!tc) break` path is dead code but harmless.

**STATUS: MATCH**

---

## SECTION B: Stereo detection (Win 649-669, SDL3 598-617)

**What it does:**
Polls all sync sources and (conditionally) async sources to determine if stereo
mixing is needed for this frame. Sets `needStereo` and `needMono` flags.

**Windows:**
```
needStereo = (right != nullptr) || mpEdgePlayer->IsStereoMixingRequired();
for sync sources: if RequiresStereoMixingNow() -> needStereo, else needMono
if (mpAudioTap):          // <-- ONLY when audio tap present
    for async sources: same check
```

**Why conditional on mpAudioTap?**
Without a tap, async sources are mixed at the resampled output rate (48kHz) in
a separate buffer, so they don't affect the ~64kHz filter stereo mode. With a
tap, async sources mix directly into the source buffer at 64kHz, so they DO
affect stereo mode.

**SDL3:**
```
needStereo = (right != nullptr) || mpEdgePlayer->IsStereoMixingRequired();
for sync sources: same
for async sources: always checked (no mpAudioTap guard)
```

**SDL3 rationale:** SDL3 always mixes async sources at mixing rate (no
resampler), so async sources always affect stereo mode. This matches the
Windows audio-tap-present path.

**STATUS: MATCH (architecturally correct divergence)**

---

## SECTION C: Stereo switch (Win 672-676, SDL3 619-631)

**What it does:**
When switching from mono to stereo, copies filter state and buffer data from
channel 0 to channel 1.

**Windows:**
```
mFilters[1].CopyState(mFilters[0]);
memcpy(mSourceBuffer[1], mSourceBuffer[0], sizeof(float) * mBufferLevel);
```

**Why `mBufferLevel`?**
In Windows, `mBufferLevel` is typically non-zero because the resampler doesn't
consume all samples every frame. The unconsumed tail at positions 0..mBufferLevel-1
contains data that will be resampled next frame. Copying it ensures channel 1
starts with the same data.

**Important:** The kPreFilterOffset overlap zone (positions 0..47 when
mBufferLevel=0) is NOT explicitly copied. In Windows, mBufferLevel is usually
large enough to cover some of this region. But if mBufferLevel happened to be
small (or 0), the overlap wouldn't be copied.

**SDL3:**
```
mFilters[1].CopyState(mFilters[0]);
memcpy(mSourceBuffer[1], mSourceBuffer[0],
       sizeof(float) * (mBufferLevel + kPreFilterOffset));
```

**SDL3 difference:** Explicitly copies the kPreFilterOffset overlap zone because
mBufferLevel is always 0 in SDL3. This is MORE correct than Windows — it ensures
the filter overlap data is always initialized for channel 1.

**STATUS: SDL3 IMPROVED (fixes latent Windows bug for mBufferLevel=0 case)**

---

## SECTION D: Internal audio taps (Win 680-683, SDL3 627-631)

**What it does:**
Notifies internal audio taps with the raw POKEY data before any mixing.
Used by the SAP recorder to capture pure POKEY output.

**Windows:** `taps->WriteInternalAudio(left, count, timestamp);`
**SDL3:** `tap->WriteInternalAudio(left, count, timestamp);`

**STATUS: MATCH**

---

## SECTION E: Copy POKEY data into source buffer (Win 686-711, SDL3 633-659)

**What it does:**
Copies the incoming POKEY L/R samples into the source buffer at position
`mBufferLevel + kPreFilterOffset`. Three modes:
1. **Blocked:** Zero-fill (internal audio muted for recording)
2. **Stereo-as-mono:** Average L+R into both channels
3. **Normal:** Copy L to left, R (or L if no R) to right

**Windows line 693 BUG:** `memcpy(dstRight + kPreFilterOffset, 0, ...)` passes
0 (null) as source. Should be `memset`. Would crash if mBlockInternalAudioCount > 0
and mbFilterStereo is true.

**SDL3 line 641:** Correctly uses `memset`.

**STATUS: SDL3 FIXED (Windows bug)**

---

## SECTION F: Sync source mixing (Win 714-772, SDL3 661-715)

**What it does:**
Creates `ATSyncAudioMixInfo` and has all sync sources mix their audio
additively into the buffer. Two sub-modes:

1. **Stereo path (mbFilterStereo):** Mix mono sources first into
   `mMonoMixBuffer`, fold into both channels. Then mix stereo sources directly
   into L/R buffers. Sources are sorted at query time via
   `RequiresStereoMixingNow()`.

2. **Mono path:** All sources mix into left buffer only.

**Key fields in mixInfo:**
- `mStartTime = timestamp` — cycle-accurate start time
- `mCount = count` — number of samples
- `mMixingRate = mMixingRate` — float, ~63920 Hz
- `mpMixLevels = mMixLevels` — per-bus volume array
- `mpDCLeft/Right` — DC offset accumulators for filter

**Windows and SDL3 are identical here.**

**STATUS: MATCH**

---

## SECTION G: Pre-filter differencing (Win 774-779, SDL3 717-722)

**What it does:**
First stage of DC removal. Computes differences between consecutive samples.
`PreFilterDiff` maintains `mDiffHistory` state across calls.

Position: `mSourceBuffer[ch][mBufferLevel + kPreFilterOffset]`

**STATUS: MATCH**

---

## SECTION H: Edge rendering (Win 781-791, SDL3 724-734)

**What it does:**
Renders speaker clicks, covox output, cassette audio edges into the buffer
BETWEEN the differencing and integration stages. This is the key insight of
the DC removal architecture — edges are expressed as deltas that integrate
into the waveform, avoiding explicit pulse expansion.

Steps:
1. `mpEdgePlayer->RenderEdges(left, right, count, timestamp)` — renders
   loose edges and edge buffers with triangle filtering
2. `mpEdgeSamplePlayer->AsSource().WriteAudio(mixInfo)` — renders
   convolution-player sounds (e.g., speaker click sample convolved
   with square wave)

**STATUS: MATCH**

---

## SECTION I: Pre-filter integration + LPF (Win 793-801, SDL3 736-743)

**What it does:**
1. `PreFilterEdges(buf, count, dcDelta)` — Integrates the differenced+edge
   signal, applies DC removal high-pass. The `dcDelta` is the change in DC
   level from the previous frame (from sync sources reporting DC offsets).
2. `Filter(buf, count)` — FIR low-pass at ~15kHz to prevent aliasing.

**DC level tracking:**
```
dcDelta = dcLevels[ch] - mPrevDCLevels[ch]
mPrevDCLevels[ch] = dcLevels[ch]   // save for next frame
```

**STATUS: MATCH**

---

## SECTION J: Stereo-to-mono fallback (Win 803-811, SDL3 746-761)

**What it does:**
If currently in stereo mode but no source needs stereo AND the two filter
states have converged (within 1e-10), count mono samples. After accumulating
`kBufferSize` consecutive mono samples, switch back to mono. This reduces
CPU by halving the filter work.

**STATUS: MATCH**

---

## SECTION K: Async source mixing + audio tap (Win 813-832, SDL3 763-784)

**What it does (Windows with mpAudioTap):**
When an audio tap is present, async sources mix at ~64kHz (mixing rate) directly
into the filtered source buffer. Then the combined result is forwarded to the
audio tap for recording.

**What it does (Windows WITHOUT mpAudioTap):**
Async sources DON'T mix here. Instead, they mix later at the resampled output
rate (~48kHz) into `mAsyncMixBuffer` (lines 873-887). The async buffer is then
added during resampling via the `Add16` variants.

**SDL3:**
Always mixes async sources at mixing rate (the tap-present path), regardless
of whether a tap is actually present. This is correct because SDL3 doesn't
resample internally — there is no output-rate buffer to mix into.

The audio tap forwarding uses `mBufferLevel + kFilterOffset` as the buffer
position. This is BEFORE `mBufferLevel += count`, so it points to the start
of the current frame's filtered output.

**STATUS: MATCH (correct architectural choice)**

---

## SECTION L: Buffer level increment (Win 834-835, SDL3 786-787)

```
mBufferLevel += count;
VDASSERT(mBufferLevel <= kBufferSize);
```

**STATUS: MATCH**

---

## SECTION M: Resampling (Win 837-922) — WINDOWS ONLY, NOT IN SDL3

**What it does:**
1. Check if the audio device's output rate changed (lines 837-844)
2. Compute how many output samples can be produced from the accumulated
   buffer using the polyphase FIR resampler (lines 846-854)
3. Allocate output and async mix buffers (lines 855-868)
4. If no audio tap: mix async sources into async buffer at OUTPUT rate (873-887)
5. Resample source buffer from ~64kHz to ~48kHz, producing sint16 stereo.
   Four variants depending on stereo/mono and async presence (889-902)
6. Shift source buffer: move unconsumed samples + overlap to position 0 (904-920)

**Buffer shift formula:**
```
shift = min(mResampleAccum >> 32, mBufferLevel)
bytesToShift = (mBufferLevel - shift + kPreFilterOffset) * sizeof(float)
memmove(src[0], src[0] + shift, bytesToShift)
mBufferLevel -= shift
mResampleAccum -= shift << 32
```

This preserves `mBufferLevel - shift` unconsumed samples PLUS `kPreFilterOffset`
overlap for the filter.

**SDL3 equivalent (lines 789-859):**
No resampling. Instead:
1. Check queue depth for overflow protection (drop if too full)
2. Interleave L/R float samples from mSourceBuffer at kFilterOffset
3. Push to SDL_AudioStream (SDL3 resamples internally)
4. Shift: move kPreFilterOffset overlap to position 0, mBufferLevel = 0

**SDL3 shift formula:**
```
// Since we output ALL of mBufferLevel, shift = mBufferLevel, so:
bytesToShift = kPreFilterOffset * sizeof(float)
memmove(src[0], src[0] + mBufferLevel, bytesToShift)
mBufferLevel = 0
```

This is mathematically equivalent to the Windows formula when `shift = mBufferLevel`:
```
bytesToShift = (mBufferLevel - mBufferLevel + kPreFilterOffset) = kPreFilterOffset
```

**CRITICAL: SDL3 always moves the shift outside the `if (mpStream)` guard,
so the buffer is always reset even if the audio device is null. Windows
always has an mpAudioOut (goes silent on failure) so this edge case doesn't
exist there.**

**STATUS: MATCH (correct simplified equivalent)**

---

## SECTION N: Buffer level management / drop logic (Win 927-1024)

**What it does (Windows):**
Sophisticated adaptive latency control using the Win32 audio backend:
1. Query hardware buffer level (line 928)
2. Track min/max levels over 15-frame windows (930-934)
3. Adjust latency targets for XAudio2/WASAPI (939-942)
4. Every 15 frames: check if latency is consistently too high → set drop flag.
   Only drop if there have been NO underflows (lines 944-967)
5. Profile incoming rate every 200 frames (982-990)
6. On underflow: push the resampled block + reset drop counter (992-1000)
7. On drop: skip writing the block (1002-1003)
8. Normal: write block, possibly repeated for time-stretching (1004-1017)
9. On overflow: increment counter, don't write (1018-1021)
10. Flush audio device (1024)

**SDL3 equivalent (lines 861-885):**
Much simpler since SDL_AudioStream handles buffering:
1. Query SDL queue depth (line 863)
2. Every 15 frames: update audio status with current queue depth (865-874)
3. Profile incoming rate every 200 frames (876-884)
4. Drop logic is handled earlier in Section M (lines 790-843): if queue
   exceeds max, the block is dropped before being pushed.

**Differences:**
- No min/max tracking over 15-frame windows (SDL3 uses instantaneous queue depth)
- No sophisticated drop counter (must be consistently high for 10 checks before
  dropping). SDL3 drops immediately when queue exceeds threshold.
- No underflow recovery (extra writes on underflow). SDL3 relies on SDL_AudioStream
  internal buffering.
- No repeat/time-stretch logic (`mRepeatAccum`/`mRepeatInc`). Not needed because
  SDL_AudioStream handles rate adaptation via its internal resampler.

**STATUS: ACCEPTABLE DIVERGENCE — SDL3 delegates buffering to SDL_AudioStream**

---

## SECTION O: Members comparison

| Windows member | SDL3 equivalent | Notes |
|---|---|---|
| mBufferLevel | mBufferLevel | Same; SDL3 always resets to 0 |
| mFilteredSampleCount | (not needed) | Only used by Windows resampler |
| mResampleAccum | (not needed) | Polyphase resampler state |
| mResampleRate | (not needed) | Polyphase resampler rate |
| mTickRate | mTickRate | Same |
| mMixingRate | mMixingRate | Same |
| mSamplingRate | (not needed) | Output device rate (SDL3 handles) |
| mSelectedApi / mActiveApi | (not needed) | SDL3 uses SDL_AudioStream |
| mPauseCount | mPauseCount | Same |
| mLatencyTargetMin/Max | (not needed) | Computed from mLatency for Win32 |
| mLatency | mLatency | Same; SDL3 clamps [10,500] |
| mExtraBuffer | mExtraBuffer | Same; SDL3 clamps [10,500] |
| mbMute | mbMute | Same |
| mbNativeAudioEnabled | (not needed) | SDL3 always enables on InitNativeAudio |
| mBlockInternalAudioCount | mBlockInternalAudioCount | Same |
| mbAsyncMixBufferZeroed | (not needed) | Output-rate async mix optimization |
| mbFilterStereo | mbFilterStereo | Same |
| mFilterMonoSamples | mFilterMonoSamples | Same |
| mRepeatAccum/Inc | (not needed) | Time-stretch for Win32 output |
| mCheckCounter | mCheckCounter | Same |
| mMinLevel/mMaxLevel | (not needed) | 15-frame level tracking for Win32 |
| mUnderflowCount | mUnderflowCount | Same |
| mOverflowCount | mOverflowCount | Same |
| mDropCounter | (not needed) | Gradual drop for Win32 |
| mWritePosition | mWritePosition | Same |
| mProfileCounter | mProfileCounter | Same |
| mProfileBlockStartPos | mProfileBlockStartPos | Same |
| mProfileBlockStartTime | mProfileBlockStartTime | Same |
| mpAudioOut | mpStream (SDL_AudioStream*) | Different backend |
| mpInternalAudioTaps | mpInternalAudioTaps | Same |
| mpAudioTap | mpAudioTap | Same |
| mpSamplePool | mpSamplePool | Same |
| mpSamplePlayer | mpSamplePlayer | Same |
| mpEdgeSamplePlayer | mpEdgeSamplePlayer | Same |
| mpAsyncSamplePlayer | mpAsyncSamplePlayer | Same |
| mpEdgePlayer | mpEdgePlayer | Same |
| mPrevDCLevels[2] | mPrevDCLevels[2] | Same |
| mAudioStatus | mAudioStatus | Same |
| mFilters[2] | mFilters[2] | Same |
| mSyncAudioSources | mSyncAudioSources | Same |
| mSyncAudioSourcesStereo | mSyncAudioSourcesStereo | Same |
| mAsyncAudioSources | mAsyncAudioSources | Same |
| mMixLevels[] | mMixLevels[] | Same |
| mSourceBuffer[2][] | mSourceBuffer[2][] | Same layout + alignment |
| mMonoMixBuffer[] | mMonoMixBuffer[] | Same |
| mAsyncMixBuffer | (not needed) | Output-rate async mixing |
| mOutputBuffer16 | (not needed) | sint16 output for Win32 |
| VDAlignedObject<16> | (not inherited) | x86_64 `new` aligns to 16 anyway |
| — | mMixingRateInt | SDL3-specific: integer rate for SDL_AudioStream |

---

## Issues found from this analysis

All previously identified and fixed:
1. Stereo switch overlap copy — FIXED
2. Buffer flush when mpStream null — FIXED
3. Turbo overflow protection — FIXED

No new issues identified.
