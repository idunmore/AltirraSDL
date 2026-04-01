//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 audio output implementation
//
//	Implements IATAudioOutput for SDL3 using SDL_AudioStream for
//	playback and automatic resampling from the POKEY mixing rate
//	(~64kHz) to the hardware output rate.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <string.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/refcount.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/audiomixer.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/audiosamplepool.h>

// -------------------------------------------------------------------------
// Stub implementations of required sub-interfaces
//
// These stubs satisfy the IATAudioMixer interface so emulation components
// can register audio sources without crashing.  The stub mixer does not
// actually mix additional audio (disk sounds, speaker clicks, etc.) --
// only the core POKEY output passes through WriteAudio().
// -------------------------------------------------------------------------

class ATStubSoundGroup final : public IATAudioSoundGroup {
	int mRefCount = 1;
public:
	int AddRef() override { return ++mRefCount; }
	int Release() override { int n = --mRefCount; if (!n) delete this; return n; }
	bool IsAnySoundQueued() const override { return false; }
	void StopAllSounds() override {}
};

class ATStubSyncSamplePlayer final : public IATSyncAudioSamplePlayer, public IATSyncAudioSource {
public:
	// IATSyncAudioSource
	bool RequiresStereoMixingNow() const override { return false; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override {}

	// IATSyncAudioSamplePlayer
	IATSyncAudioSource& AsSource() override { return *this; }
	vdrefptr<IATAudioSampleHandle> RegisterSample(vdspan<const sint16>, const ATAudioSoundSamplingRate&, float) override { return {}; }
	ATSoundId AddSound(IATAudioSoundGroup&, uint32, ATAudioSampleId, float) override { return ATSoundId::Invalid; }
	ATSoundId AddLoopingSound(IATAudioSoundGroup&, uint32, ATAudioSampleId, float) override { return ATSoundId::Invalid; }
	ATSoundId AddSound(IATAudioSoundGroup&, uint32, IATAudioSampleSource*, IVDRefCount*, uint32, float) override { return ATSoundId::Invalid; }
	ATSoundId AddLoopingSound(IATAudioSoundGroup&, uint32, IATAudioSampleSource*, IVDRefCount*, float) override { return ATSoundId::Invalid; }
	ATSoundId AddSound(IATAudioSoundGroup&, uint32, IATAudioSampleHandle&, const ATSoundParams&) override { return ATSoundId::Invalid; }
	vdrefptr<IATAudioSoundGroup> CreateGroup(const ATAudioGroupDesc&) override { return vdrefptr<IATAudioSoundGroup>(new ATStubSoundGroup); }
	void ForceStopSound(ATSoundId) override {}
	void StopSound(ATSoundId) override {}
	void StopSound(ATSoundId, uint64) override {}
	vdrefptr<IATSyncAudioConvolutionPlayer> CreateConvolutionPlayer(ATAudioSampleId) override { return {}; }
	vdrefptr<IATSyncAudioConvolutionPlayer> CreateConvolutionPlayer(const sint16*, uint32) override { return {}; }
};

class ATStubEdgePlayer final : public IATSyncAudioEdgePlayer {
public:
	void AddEdges(const ATSyncAudioEdge*, size_t, float) override {}
	void AddEdgeBuffer(ATSyncAudioEdgeBuffer*) override {}
};

// -------------------------------------------------------------------------
// Stub audio mixer
// -------------------------------------------------------------------------

class ATAudioMixerStub final : public IATAudioMixer {
public:
	void AddSyncAudioSource(IATSyncAudioSource*) override {}
	void RemoveSyncAudioSource(IATSyncAudioSource*) override {}
	void AddAsyncAudioSource(IATAudioAsyncSource&) override {}
	void RemoveAsyncAudioSource(IATAudioAsyncSource&) override {}

	IATSyncAudioSamplePlayer& GetSamplePlayer() override { return mSamplePlayer; }
	IATSyncAudioSamplePlayer& GetEdgeSamplePlayer() override { return mSamplePlayer; }
	IATSyncAudioEdgePlayer& GetEdgePlayer() override { return mEdgePlayer; }
	IATSyncAudioSamplePlayer& GetAsyncSamplePlayer() override { return mSamplePlayer; }

	void AddInternalAudioTap(IATInternalAudioTap*) override {}
	void RemoveInternalAudioTap(IATInternalAudioTap*) override {}
	void BlockInternalAudio() override {}
	void UnblockInternalAudio() override {}

private:
	ATStubSyncSamplePlayer mSamplePlayer;
	ATStubEdgePlayer mEdgePlayer;
};

// -------------------------------------------------------------------------
// ATAudioOutputSDL3 — functional audio output via SDL3 audio stream
// -------------------------------------------------------------------------

class ATAudioOutputSDL3 final : public IATAudioOutput {
public:
	~ATAudioOutputSDL3();

	void Init(ATScheduler&) override {
		// Match Windows ATAudioOutput::Init which calls SetCyclesPerSecond here.
		SetCyclesPerSecond(1789772.5, 1.0);
	}
	void InitNativeAudio() override;

	ATAudioApi GetApi() override { return kATAudioApi_Auto; }
	void SetApi(ATAudioApi) override {}

	void SetAudioTap(IATAudioTap* tap) override { mpAudioTap = tap; }
	ATUIAudioStatus GetAudioStatus() const override;

	IATAudioMixer& AsMixer() override { return mMixer; }
	ATAudioSamplePool& GetPool() override { return mPool; }

	void SetCyclesPerSecond(double cps, double repeatfactor) override;

	bool GetMute() override { return mMuted; }
	void SetMute(bool mute) override;
	float GetVolume() override { return mVolume; }
	void SetVolume(float v) override { mVolume = v; }

	float GetMixLevel(ATAudioMix mix) const override;
	void SetMixLevel(ATAudioMix mix, float level) override;

	int GetLatency() override { return mLatencyMs; }
	void SetLatency(int ms) override { mLatencyMs = ms; }
	int GetExtraBuffer() override { return mExtraBufferMs; }
	void SetExtraBuffer(int ms) override { mExtraBufferMs = ms; }
	void SetFiltersEnabled(bool) override {}

	void Pause() override;
	void Resume() override;

	void WriteAudio(const float* left, const float* right,
	                uint32 count, bool pushAudio, bool pushStereoAsAudio,
	                uint64 timestamp) override;

private:
	SDL_AudioStream* mpStream = nullptr;
	ATAudioMixerStub mMixer;
	ATAudioSamplePool mPool;
	IATAudioTap* mpAudioTap = nullptr;

	float mVolume = 1.0f;
	// Match Windows defaults from ATAudioOutput constructor
	float mMixLevels[kATAudioMixCount] {
		0.8f,   // kATAudioMix_Drive
		1.0f,   // kATAudioMix_Covox
		0.7f,   // kATAudioMix_Modem
		0.5f,   // kATAudioMix_Cassette
		1.0f,   // kATAudioMix_Other
	};
	int mLatencyMs = 40;
	int mExtraBufferMs = 0;
	int mMixingRate = 63920;  // cps / 28, updated by SetCyclesPerSecond
	bool mMuted = false;
	bool mPaused = false;

	int mUnderflowCount = 0;
	int mOverflowCount = 0;
};

ATAudioOutputSDL3::~ATAudioOutputSDL3() {
	if (mpStream) {
		SDL_DestroyAudioStream(mpStream);
		mpStream = nullptr;
	}
}

void ATAudioOutputSDL3::InitNativeAudio() {
	// Create a simplified audio device + stream.  We specify the app-side
	// format (POKEY mixing rate, float32 stereo).  SDL3 handles conversion
	// to whatever the hardware device needs.
	SDL_AudioSpec spec {};
	spec.freq = mMixingRate;
	spec.format = SDL_AUDIO_F32;
	spec.channels = 2;

	mpStream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

	if (!mpStream) {
		fprintf(stderr, "[ATAudioOutputSDL3] Failed to open audio: %s\n",
		        SDL_GetError());
		return;
	}

	// SDL_OpenAudioDeviceStream starts paused; resume immediately.
	SDL_ResumeAudioStreamDevice(mpStream);

	fprintf(stderr, "[ATAudioOutputSDL3] Audio initialized at %d Hz stereo\n",
	        mMixingRate);
}

ATUIAudioStatus ATAudioOutputSDL3::GetAudioStatus() const {
	ATUIAudioStatus status {};
	status.mUnderflowCount = mUnderflowCount;
	status.mOverflowCount = mOverflowCount;
	status.mDropCount = 0;
	status.mMeasuredMin = 0;
	status.mMeasuredMax = 0;
	status.mTargetMin = mLatencyMs;
	status.mTargetMax = mLatencyMs + mExtraBufferMs;
	status.mIncomingRate = (double)mMixingRate;
	status.mExpectedRate = (double)mMixingRate;
	status.mSamplingRate = 48000.0;
	status.mbStereoMixing = false;
	return status;
}

void ATAudioOutputSDL3::SetCyclesPerSecond(double cps, double repeatfactor) {
	int newRate = (int)(cps / 28.0 + 0.5);

	if (newRate == mMixingRate)
		return;

	mMixingRate = newRate;

	// Update the stream's input format to match the new POKEY rate.
	// SDL3 handles the resampling to the device's output rate.
	if (mpStream) {
		SDL_AudioSpec srcSpec {};
		srcSpec.freq = mMixingRate;
		srcSpec.format = SDL_AUDIO_F32;
		srcSpec.channels = 2;
		SDL_SetAudioStreamFormat(mpStream, &srcSpec, nullptr);
	}
}

void ATAudioOutputSDL3::SetMute(bool mute) {
	mMuted = mute;
}

float ATAudioOutputSDL3::GetMixLevel(ATAudioMix mix) const {
	if ((unsigned)mix < kATAudioMixCount)
		return mMixLevels[mix];
	return 1.0f;
}

void ATAudioOutputSDL3::SetMixLevel(ATAudioMix mix, float level) {
	if ((unsigned)mix < kATAudioMixCount)
		mMixLevels[mix] = level;
}

void ATAudioOutputSDL3::Pause() {
	mPaused = true;
	if (mpStream)
		SDL_PauseAudioStreamDevice(mpStream);
}

void ATAudioOutputSDL3::Resume() {
	mPaused = false;
	if (mpStream)
		SDL_ResumeAudioStreamDevice(mpStream);
}

void ATAudioOutputSDL3::WriteAudio(
	const float* left,
	const float* right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsAudio,
	uint64 timestamp)
{
	if (!count || !mpStream)
		return;

	// Forward raw audio to the tap (for recording) before volume scaling
	if (mpAudioTap)
		mpAudioTap->WriteRawAudio(left, right, count, (uint32)timestamp);

	// Compute effective volume (0 when muted)
	const float vol = mMuted ? 0.0f : mVolume;

	// Interleave left/right into stereo pairs and apply volume.
	// Process in chunks to avoid excessive stack usage.
	static constexpr uint32 kChunkSize = 1024;
	float interleaved[kChunkSize * 2];

	const float* r = right;
	const float* l = left;
	uint32 remaining = count;

	while (remaining > 0) {
		uint32 n = remaining < kChunkSize ? remaining : kChunkSize;

		if (r) {
			for (uint32 i = 0; i < n; ++i) {
				interleaved[i * 2    ] = l[i] * vol;
				interleaved[i * 2 + 1] = r[i] * vol;
			}
		} else {
			// Mono: duplicate left to both channels
			for (uint32 i = 0; i < n; ++i) {
				interleaved[i * 2    ] = l[i] * vol;
				interleaved[i * 2 + 1] = l[i] * vol;
			}
		}

		SDL_PutAudioStreamData(mpStream, interleaved,
		                       (int)(n * 2 * sizeof(float)));

		l += n;
		if (r) r += n;
		remaining -= n;
	}

	// Monitor queue depth to detect underflow/overflow.
	// Each sample frame is 2 floats (stereo) = 8 bytes.
	int queued = SDL_GetAudioStreamQueued(mpStream);
	int maxQueueBytes = mMixingRate * 2 * (int)sizeof(float)
	                    * (mLatencyMs + mExtraBufferMs + 50) / 1000;

	if (queued > maxQueueBytes)
		++mOverflowCount;
	else if (queued == 0 && count > 0)
		++mUnderflowCount;
}

// -------------------------------------------------------------------------
// Factory function — replaces the Windows ATCreateAudioOutput()
// -------------------------------------------------------------------------

IATAudioOutput *ATCreateAudioOutput() {
	return new ATAudioOutputSDL3();
}
