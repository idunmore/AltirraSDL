//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 audio output — full mixer implementation
//
//	This implements IATAudioOutput + IATAudioMixer for SDL3, matching the
//	Windows ATAudioOutput mixing pipeline:
//	  POKEY → sync sources → edge player → DC removal → LPF → async sources → SDL3 stream
//
//	The key difference from Windows is that we skip the internal polyphase
//	resampler: filtered ~64kHz float samples go directly to SDL_AudioStream,
//	which handles resampling to the hardware device rate.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <string.h>
#include <algorithm>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/refcount.h>
#include <vd2/system/time.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/audiomixer.h>
#include <at/ataudio/audiofilters.h>
#include <at/ataudio/audiosampleplayer.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/audiosamplepool.h>

// =========================================================================
// ATSyncAudioEdgePlayer — copied from audiooutput.cpp (platform-independent)
//
// Converts high-rate edge transitions into audio samples via triangle
// filtering. Inserted between the differencing and integration stages
// of the DC removal filter.
// =========================================================================

class ATSyncAudioEdgePlayer final : public IATSyncAudioEdgePlayer {
public:
	static constexpr int kTailLength = 3;

	bool IsStereoMixingRequired() const;

	void RenderEdges(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp);

	void AddEdges(const ATSyncAudioEdge *edges, size_t numEdges, float volume) override;
	void AddEdgeBuffer(ATSyncAudioEdgeBuffer *buffer) override;

protected:
	void RenderEdgeBuffer(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume);

	template<bool T_RightEnabled>
	void RenderEdgeBuffer2(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume);

	vdfastvector<ATSyncAudioEdge> mEdges;
	vdfastvector<ATSyncAudioEdgeBuffer *> mBuffers;
	bool mbTailHasStereo = false;

	float mLeftTail[kTailLength] {};
	float mRightTail[kTailLength] {};
};

bool ATSyncAudioEdgePlayer::IsStereoMixingRequired() const {
	if (mbTailHasStereo)
		return true;

	if (mBuffers.empty())
		return false;

	for(ATSyncAudioEdgeBuffer *buf : mBuffers) {
		if (!buf->mEdges.empty() && buf->mLeftVolume != buf->mRightVolume && buf->mLeftVolume != 0)
			return true;
	}

	return false;
}

void ATSyncAudioEdgePlayer::RenderEdges(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp) {
	memset(dstLeft + n, 0, sizeof(*dstLeft) * kTailLength);

	for(int i=0; i<kTailLength; ++i)
		dstLeft[i] += mLeftTail[i];

	if (dstRight) {
		memset(dstRight + n, 0, sizeof(*dstRight) * kTailLength);

		for(int i=0; i<kTailLength; ++i)
			dstRight[i] += mRightTail[i];
	}

	RenderEdgeBuffer(dstLeft, dstRight, n, timestamp, mEdges.data(), mEdges.size(), 1.0f);
	mEdges.clear();

	while(!mBuffers.empty()) {
		ATSyncAudioEdgeBuffer *buf = mBuffers.back();
		mBuffers.pop_back();

		if (buf->mLeftVolume == buf->mRightVolume) {
			if (buf->mLeftVolume != 0)
				RenderEdgeBuffer(dstLeft, dstRight, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mLeftVolume);
		} else {
			if (buf->mLeftVolume != 0)
				RenderEdgeBuffer(dstLeft, nullptr, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mLeftVolume);

			if (buf->mRightVolume != 0) {
				if (dstRight) {
					RenderEdgeBuffer(dstRight, nullptr, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mRightVolume);
				} else {
					RenderEdgeBuffer(dstLeft, nullptr, n, timestamp, buf->mEdges.data(), buf->mEdges.size(), buf->mRightVolume);
				}
			}
		}

		buf->mEdges.clear();
		buf->Release();
	}

	for(int i=0; i<kTailLength; ++i)
		mLeftTail[i] = dstLeft[n + i];

	if (dstRight) {
		for(int i=0; i<kTailLength; ++i)
			mRightTail[i] = dstRight[n + i];
	} else {
		for(int i=0; i<kTailLength; ++i)
			mRightTail[i] = dstLeft[n + i];
	}

	mbTailHasStereo = memcmp(mLeftTail, mRightTail, sizeof mLeftTail) != 0;
}

void ATSyncAudioEdgePlayer::AddEdges(const ATSyncAudioEdge *edges, size_t numEdges, float volume) {
	if (!numEdges)
		return;

	mEdges.resize(mEdges.size() + numEdges);

	const ATSyncAudioEdge *VDRESTRICT src = edges;
	ATSyncAudioEdge *VDRESTRICT dst = &*(mEdges.end() - numEdges);

	while(numEdges--) {
		dst->mTime = src->mTime;
		dst->mDeltaValue = src->mDeltaValue * volume;
		++dst;
		++src;
	}
}

void ATSyncAudioEdgePlayer::AddEdgeBuffer(ATSyncAudioEdgeBuffer *buffer) {
	if (buffer) {
		mBuffers.push_back(buffer);
		buffer->AddRef();
	}
}

void ATSyncAudioEdgePlayer::RenderEdgeBuffer(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume) {
	if (dstRight)
		RenderEdgeBuffer2<true>(dstLeft, dstRight, n, timestamp, edges, numEdges, volume);
	else
		RenderEdgeBuffer2<false>(dstLeft, dstRight, n, timestamp, edges, numEdges, volume);
}

template<bool T_RightEnabled>
void ATSyncAudioEdgePlayer::RenderEdgeBuffer2(float *dstLeft, float *dstRight, uint32 n, uint32 timestamp, const ATSyncAudioEdge *edges, size_t numEdges, float volume) {
	const ATSyncAudioEdge *VDRESTRICT src = edges;
	float *VDRESTRICT dstL2 = dstLeft;
	float *VDRESTRICT dstR2 = dstRight;

	const uint32 timeWindow = (n+2) * 28;

	while(numEdges--) {
		const uint32 cycleOffset = src->mTime - timestamp;
		if (cycleOffset < timeWindow) {
			const uint32 sampleOffset = cycleOffset / 28;
			const uint32 phaseOffset = cycleOffset % 28;
			const float shift = (float)phaseOffset * (1.0f / 28.0f);
			const float delta = src->mDeltaValue * volume;
			const float v1 = delta * shift;
			const float v0 = delta - v1;

			dstL2[sampleOffset+0] += v0;
			dstL2[sampleOffset+1] += v1;

			if constexpr (T_RightEnabled) {
				dstR2[sampleOffset+0] += v0;
				dstR2[sampleOffset+1] += v1;
			}
		} else {
			if (cycleOffset & 0x80000000)
				VDFAIL("Edge player has sample before allowed frame window.");
			else
				VDFAIL("Edge player has sample after allowed frame window.");
		}

		++src;
	}
}

// =========================================================================
// ATAudioOutputSDL3 — full mixer + SDL3 audio stream output
// =========================================================================

class ATAudioOutputSDL3 final : public IATAudioOutput, public IATAudioMixer {
	ATAudioOutputSDL3(const ATAudioOutputSDL3&) = delete;
	ATAudioOutputSDL3& operator=(const ATAudioOutputSDL3&) = delete;

public:
	ATAudioOutputSDL3();
	~ATAudioOutputSDL3();

	// IATAudioOutput
	void Init(ATScheduler& scheduler) override;
	void InitNativeAudio() override;

	ATAudioApi GetApi() override { return kATAudioApi_Auto; }
	void SetApi(ATAudioApi) override {}

	void SetAudioTap(IATAudioTap* tap) override { mpAudioTap = tap; }
	ATUIAudioStatus GetAudioStatus() const override { return mAudioStatus; }

	IATAudioMixer& AsMixer() override { return *this; }
	ATAudioSamplePool& GetPool() override { return *mpSamplePool; }

	void SetCyclesPerSecond(double cps, double repeatfactor) override;

	bool GetMute() override { return mbMute; }
	void SetMute(bool mute) override { mbMute = mute; }

	float GetVolume() override { return mFilters[0].GetScale(); }
	void SetVolume(float vol) override {
		mFilters[0].SetScale(vol);
		mFilters[1].SetScale(vol);
	}

	float GetMixLevel(ATAudioMix mix) const override;
	void SetMixLevel(ATAudioMix mix, float level) override;

	int GetLatency() override { return mLatency; }
	void SetLatency(int ms) override {
		if (ms < 10) ms = 10;
		else if (ms > 500) ms = 500;
		mLatency = ms;
	}
	int GetExtraBuffer() override { return mExtraBuffer; }
	void SetExtraBuffer(int ms) override {
		if (ms < 10) ms = 10;
		else if (ms > 500) ms = 500;
		mExtraBuffer = ms;
	}

	void SetFiltersEnabled(bool enable) override {
		mFilters[0].SetActiveMode(enable);
		mFilters[1].SetActiveMode(enable);
	}

	void Pause() override;
	void Resume() override;

	void WriteAudio(const float* left, const float* right,
	                uint32 count, bool pushAudio, bool pushStereoAsMono,
	                uint64 timestamp) override;

	// IATAudioMixer
	void AddSyncAudioSource(IATSyncAudioSource* src) override;
	void RemoveSyncAudioSource(IATSyncAudioSource* src) override;
	void AddAsyncAudioSource(IATAudioAsyncSource& src) override;
	void RemoveAsyncAudioSource(IATAudioAsyncSource& src) override;

	IATSyncAudioSamplePlayer& GetSamplePlayer() override { return *mpSamplePlayer; }
	IATSyncAudioSamplePlayer& GetEdgeSamplePlayer() override { return *mpEdgeSamplePlayer; }
	IATSyncAudioEdgePlayer& GetEdgePlayer() override { return *mpEdgePlayer; }
	IATSyncAudioSamplePlayer& GetAsyncSamplePlayer() override { return *mpAsyncSamplePlayer; }

	void AddInternalAudioTap(IATInternalAudioTap* tap) override;
	void RemoveInternalAudioTap(IATInternalAudioTap* tap) override;
	void BlockInternalAudio() override { ++mBlockInternalAudioCount; }
	void UnblockInternalAudio() override { --mBlockInternalAudioCount; }

private:
	void InternalWriteAudio(const float* left, const float* right,
	                        uint32 count, bool pushAudio, bool pushStereoAsMono,
	                        uint64 timestamp);

	// Buffer sizing — matches Windows ATAudioOutput exactly
	enum {
		kBufferSize = 1536,
		kFilterOffset = 16,
		kPreFilterOffset = kFilterOffset + ATAudioFilter::kFilterOverlap * 2,
		kEdgeRenderOverlap = ATSyncAudioEdgePlayer::kTailLength,
		kSourceBufferSize = (kBufferSize + kPreFilterOffset + kEdgeRenderOverlap + 15) & ~15,
	};

	// SDL3 audio stream
	SDL_AudioStream* mpStream = nullptr;

	// Mixer components
	vdautoptr<ATAudioSamplePool> mpSamplePool;
	vdautoptr<ATAudioSamplePlayer> mpSamplePlayer;
	vdautoptr<ATAudioSamplePlayer> mpEdgeSamplePlayer;
	vdautoptr<ATAudioSamplePlayer> mpAsyncSamplePlayer;
	vdautoptr<ATSyncAudioEdgePlayer> mpEdgePlayer;

	// Audio taps
	vdautoptr<vdfastvector<IATInternalAudioTap *>> mpInternalAudioTaps;
	IATAudioTap* mpAudioTap = nullptr;

	// Audio filters (DC removal + low-pass)
	ATAudioFilter mFilters[2];
	float mPrevDCLevels[2] {};

	// Source tracking
	typedef vdfastvector<IATSyncAudioSource *> SyncAudioSources;
	SyncAudioSources mSyncAudioSources;
	SyncAudioSources mSyncAudioSourcesStereo;

	typedef vdfastvector<IATAudioAsyncSource *> AsyncAudioSources;
	AsyncAudioSources mAsyncAudioSources;

	// Mix levels
	float mMixLevels[kATAudioMixCount];

	// Source buffers — aligned for SIMD
	alignas(16) float mSourceBuffer[2][kSourceBufferSize] {};
	alignas(16) float mMonoMixBuffer[kBufferSize] {};

	// State
	uint32 mBufferLevel = 0;
	double mTickRate = 1;
	float mMixingRate = 0;
	int mLatency = 40;
	int mExtraBuffer = 0;
	bool mbMute = false;
	bool mbFilterStereo = false;
	uint32 mFilterMonoSamples = 0;
	uint32 mBlockInternalAudioCount = 0;
	uint32 mPauseCount = 0;

	// Profiling / status
	ATUIAudioStatus mAudioStatus {};
	uint32 mWritePosition = 0;
	uint32 mProfileCounter = 0;
	uint32 mProfileBlockStartPos = 0;
	uint64 mProfileBlockStartTime = 0;
	uint32 mCheckCounter = 0;
	uint32 mUnderflowCount = 0;
	uint32 mOverflowCount = 0;
	int mMixingRateInt = 63920;
};

// -------------------------------------------------------------------------
// Construction / destruction
// -------------------------------------------------------------------------

ATAudioOutputSDL3::ATAudioOutputSDL3() {
	mpEdgePlayer = new ATSyncAudioEdgePlayer;
	mpSamplePool = new ATAudioSamplePool;

	mMixLevels[kATAudioMix_Drive] = 0.8f;
	mMixLevels[kATAudioMix_Other] = 1.0f;
	mMixLevels[kATAudioMix_Covox] = 1.0f;
	mMixLevels[kATAudioMix_Cassette] = 0.5f;
	mMixLevels[kATAudioMix_Modem] = 0.7f;
}

ATAudioOutputSDL3::~ATAudioOutputSDL3() {
	if (mpStream) {
		SDL_DestroyAudioStream(mpStream);
		mpStream = nullptr;
	}
}

// -------------------------------------------------------------------------
// Init — create sample players (matches Windows ATAudioOutput::Init)
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::Init(ATScheduler& scheduler) {
	mpSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);
	mpEdgeSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);

	AddSyncAudioSource(&mpSamplePlayer->AsSource());
	// edge sample player is special cased — not added as sync source

	mpAsyncSamplePlayer = new ATAudioSamplePlayer(*mpSamplePool, scheduler);
	AddAsyncAudioSource(*mpAsyncSamplePlayer);

	mbFilterStereo = false;
	mFilterMonoSamples = 0;

	mBufferLevel = 0;

	mCheckCounter = 0;
	mUnderflowCount = 0;
	mOverflowCount = 0;

	mWritePosition = 0;

	mProfileBlockStartPos = 0;
	mProfileBlockStartTime = VDGetPreciseTick();
	mProfileCounter = 0;

	SetCyclesPerSecond(1789772.5, 1.0);
}

// -------------------------------------------------------------------------
// InitNativeAudio — create SDL3 audio stream
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::InitNativeAudio() {
	SDL_AudioSpec spec {};
	spec.freq = mMixingRateInt;
	spec.format = SDL_AUDIO_F32;
	spec.channels = 2;

	mpStream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);

	if (!mpStream) {
		fprintf(stderr, "[ATAudioOutputSDL3] Failed to open audio: %s\n",
		        SDL_GetError());
		return;
	}

	SDL_ResumeAudioStreamDevice(mpStream);

	fprintf(stderr, "[ATAudioOutputSDL3] Audio initialized at %d Hz stereo (full mixer)\n",
	        mMixingRateInt);
}

// -------------------------------------------------------------------------
// SetCyclesPerSecond — update mixing rate and sample player rates
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::SetCyclesPerSecond(double cps, double repeatfactor) {
	mTickRate = cps;
	mMixingRate = (float)(cps / 28.0);
	mAudioStatus.mExpectedRate = cps / 28.0;

	int newRate = (int)(cps / 28.0 + 0.5);
	bool rateChanged = (newRate != mMixingRateInt);
	mMixingRateInt = newRate;

	if (rateChanged && mpStream) {
		SDL_AudioSpec srcSpec {};
		srcSpec.freq = mMixingRateInt;
		srcSpec.format = SDL_AUDIO_F32;
		srcSpec.channels = 2;
		SDL_SetAudioStreamFormat(mpStream, &srcSpec, nullptr);
	}

	// Update sample player rates — matches Windows RecomputeResamplingRate()
	float pokeyMixingRate = mMixingRate;

	if (mpSamplePlayer)
		mpSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);

	if (mpEdgeSamplePlayer)
		mpEdgeSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);

	// For SDL3, async player always mixes at POKEY rate (we don't resample internally).
	// This matches the Windows audio-tap-present path.
	if (mpAsyncSamplePlayer)
		mpAsyncSamplePlayer->SetRates(pokeyMixingRate, 1.0f, 1.0f / (float)kATCyclesPerSyncSample);
}

// -------------------------------------------------------------------------
// Mix levels, pause/resume
// -------------------------------------------------------------------------

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
	if (!mPauseCount++) {
		if (mpStream)
			SDL_PauseAudioStreamDevice(mpStream);
	}
}

void ATAudioOutputSDL3::Resume() {
	if (!--mPauseCount) {
		if (mpStream)
			SDL_ResumeAudioStreamDevice(mpStream);
	}
}

// -------------------------------------------------------------------------
// Sync/async source management (IATAudioMixer)
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::AddSyncAudioSource(IATSyncAudioSource* src) {
	mSyncAudioSources.push_back(src);
}

void ATAudioOutputSDL3::RemoveSyncAudioSource(IATSyncAudioSource* src) {
	auto it = std::find(mSyncAudioSources.begin(), mSyncAudioSources.end(), src);
	if (it != mSyncAudioSources.end())
		mSyncAudioSources.erase(it);
}

void ATAudioOutputSDL3::AddAsyncAudioSource(IATAudioAsyncSource& src) {
	mAsyncAudioSources.push_back(&src);
}

void ATAudioOutputSDL3::RemoveAsyncAudioSource(IATAudioAsyncSource& src) {
	auto it = std::find(mAsyncAudioSources.begin(), mAsyncAudioSources.end(), &src);
	if (it != mAsyncAudioSources.end())
		mAsyncAudioSources.erase(it);
}

// -------------------------------------------------------------------------
// Internal audio taps
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::AddInternalAudioTap(IATInternalAudioTap* tap) {
	if (!mpInternalAudioTaps)
		mpInternalAudioTaps = new vdfastvector<IATInternalAudioTap *>;

	mpInternalAudioTaps->push_back(tap);
}

void ATAudioOutputSDL3::RemoveInternalAudioTap(IATInternalAudioTap* tap) {
	if (mpInternalAudioTaps) {
		auto it = std::find(mpInternalAudioTaps->begin(), mpInternalAudioTaps->end(), tap);

		if (it != mpInternalAudioTaps->end()) {
			*it = mpInternalAudioTaps->back();
			mpInternalAudioTaps->pop_back();

			if (mpInternalAudioTaps->empty())
				mpInternalAudioTaps = nullptr;
		}
	}
}

// -------------------------------------------------------------------------
// WriteAudio — outer loop, splits into kBufferSize chunks
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::WriteAudio(
	const float* left,
	const float* right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsMono,
	uint64 timestamp)
{
	if (!count)
		return;

	mWritePosition += count;

	for(;;) {
		uint32 tc = kBufferSize - mBufferLevel;
		if (tc > count)
			tc = count;

		InternalWriteAudio(left, right, tc, pushAudio, pushStereoAsMono, timestamp);

		if (!tc)
			break;

		count -= tc;
		if (!count)
			break;

		timestamp += 28 * tc;
		left += tc;
		if (right)
			right += tc;
	}
}

// -------------------------------------------------------------------------
// InternalWriteAudio — full mixing pipeline
//
// This closely follows ATAudioOutput::InternalWriteAudio() from the
// Windows implementation, diverging only at the output stage where we
// push float32 to SDL3 instead of resampling to sint16 for IVDAudioOutput.
// -------------------------------------------------------------------------

void ATAudioOutputSDL3::InternalWriteAudio(
	const float* left,
	const float* right,
	uint32 count,
	bool pushAudio,
	bool pushStereoAsMono,
	uint64 timestamp)
{
	VDASSERT(count > 0);
	VDASSERT(mBufferLevel + count <= kBufferSize);

	// ---- Step 1: Determine stereo requirements ----
	bool needMono = false;
	bool needStereo = right != nullptr || mpEdgePlayer->IsStereoMixingRequired();

	for(IATSyncAudioSource *src : mSyncAudioSources) {
		if (src->RequiresStereoMixingNow()) {
			needStereo = true;
		} else {
			needMono = true;
		}
	}

	// For SDL3 we always mix async sources at mixing rate (like the audio-tap path)
	for(IATAudioAsyncSource *src : mAsyncAudioSources) {
		if (src->RequiresStereoMixingNow()) {
			needStereo = true;
		} else {
			needMono = true;
		}
	}

	// Switch to stereo filtering if needed
	if (needStereo && !mbFilterStereo) {
		mFilters[1].CopyState(mFilters[0]);

		// Copy the current buffer data PLUS the kPreFilterOffset overlap zone that
		// contains filter state from the previous frame. In the Windows version,
		// mBufferLevel is typically non-zero (the resampler doesn't consume everything),
		// so the overlap zone is implicitly covered. In our SDL3 version, mBufferLevel
		// is always 0 at the start of InternalWriteAudio because we flush everything,
		// so we must explicitly copy the overlap zone.
		memcpy(mSourceBuffer[1], mSourceBuffer[0], sizeof(float) * (mBufferLevel + kPreFilterOffset));
		mbFilterStereo = true;
	}

	if (count) {
		// ---- Step 2: Notify internal audio taps ----
		if (mpInternalAudioTaps) {
			for(IATInternalAudioTap *tap : *mpInternalAudioTaps)
				tap->WriteInternalAudio(left, count, timestamp);
		}

		// ---- Step 3: Copy POKEY data into source buffer ----
		float *const dstLeft = &mSourceBuffer[0][mBufferLevel];
		float *const dstRight = mbFilterStereo ? &mSourceBuffer[1][mBufferLevel] : nullptr;

		if (mBlockInternalAudioCount) {
			memset(dstLeft + kPreFilterOffset, 0, sizeof(float) * count);

			if (mbFilterStereo)
				memset(dstRight + kPreFilterOffset, 0, sizeof(float) * count);
		} else if (mbFilterStereo && pushStereoAsMono && right) {
			float *VDRESTRICT mixDstLeft = dstLeft + kPreFilterOffset;
			float *VDRESTRICT mixDstRight = dstRight + kPreFilterOffset;
			const float *VDRESTRICT mixSrcLeft = left;
			const float *VDRESTRICT mixSrcRight = right;

			for(size_t i=0; i<count; ++i)
				mixDstLeft[i] = mixDstRight[i] = (mixSrcLeft[i] + mixSrcRight[i]) * 0.5f;
		} else {
			memcpy(dstLeft + kPreFilterOffset, left, sizeof(float) * count);

			if (mbFilterStereo) {
				if (right)
					memcpy(dstRight + kPreFilterOffset, right, sizeof(float) * count);
				else
					memcpy(dstRight + kPreFilterOffset, left, sizeof(float) * count);
			}
		}

		// ---- Step 4: Mix sync audio sources ----
		float dcLevels[2] = { 0, 0 };

		ATSyncAudioMixInfo mixInfo {};
		mixInfo.mStartTime = timestamp;
		mixInfo.mCount = count;
		mixInfo.mNumCycles = count * kATCyclesPerSyncSample;
		mixInfo.mMixingRate = mMixingRate;
		mixInfo.mpDCLeft = &dcLevels[0];
		mixInfo.mpDCRight = &dcLevels[1];
		mixInfo.mpMixLevels = mMixLevels;

		if (mbFilterStereo) {
			// Mixed mono/stereo mixing
			mSyncAudioSourcesStereo.clear();

			// Mix mono sources first
			if (needMono) {
				memset(mMonoMixBuffer, 0, sizeof(float) * count);

				mixInfo.mpLeft = mMonoMixBuffer;
				mixInfo.mpRight = nullptr;

				for(IATSyncAudioSource *src : mSyncAudioSources) {
					if (!src->RequiresStereoMixingNow())
						src->WriteAudio(mixInfo);
					else
						mSyncAudioSourcesStereo.push_back(src);
				}

				// Fold mono buffer into both stereo channels
				for(uint32 i=0; i<count; ++i) {
					float v = mMonoMixBuffer[i];
					dstLeft[kPreFilterOffset + i] += v;
					dstRight[kPreFilterOffset + i] += v;
				}

				dcLevels[1] = dcLevels[0];
			}

			// Mix stereo sources
			mixInfo.mpLeft = dstLeft + kPreFilterOffset;
			mixInfo.mpRight = dstRight + kPreFilterOffset;

			for(IATSyncAudioSource *src : needMono ? mSyncAudioSourcesStereo : mSyncAudioSources) {
				src->WriteAudio(mixInfo);
			}
		} else {
			// Mono mixing
			mixInfo.mpLeft = dstLeft + kPreFilterOffset;
			mixInfo.mpRight = nullptr;

			for(IATSyncAudioSource *src : mSyncAudioSources)
				src->WriteAudio(mixInfo);
		}

		// ---- Step 5: Pre-filter differencing (DC removal stage 1) ----
		const int nch = mbFilterStereo ? 2 : 1;
		const ptrdiff_t prefilterPos = mBufferLevel + kPreFilterOffset;
		for(int ch=0; ch<nch; ++ch) {
			mFilters[ch].PreFilterDiff(&mSourceBuffer[ch][prefilterPos], count);
		}

		// ---- Step 6: Render edges ----
		mixInfo.mpLeft = &mSourceBuffer[0][prefilterPos];
		mixInfo.mpRight = nullptr;

		if (nch > 1)
			mixInfo.mpRight = &mSourceBuffer[1][prefilterPos];

		mpEdgePlayer->RenderEdges(mixInfo.mpLeft, mixInfo.mpRight, count, (uint32)timestamp);

		if (mpEdgeSamplePlayer)
			mpEdgeSamplePlayer->AsSource().WriteAudio(mixInfo);

		// ---- Step 7: Pre-filter integration + DC removal + low-pass ----
		for(int ch=0; ch<nch; ++ch) {
			mFilters[ch].PreFilterEdges(&mSourceBuffer[ch][prefilterPos], count, dcLevels[ch] - mPrevDCLevels[ch]);
			mFilters[ch].Filter(&mSourceBuffer[ch][mBufferLevel + kFilterOffset], count);
		}

		mPrevDCLevels[0] = dcLevels[0];
		mPrevDCLevels[1] = dcLevels[1];
	}

	// ---- Step 8: Check if we can switch from stereo back to mono ----
	if (mbFilterStereo && !needStereo && mFilters[0].CloseTo(mFilters[1], 1e-10f)) {
		mFilterMonoSamples += count;

		if (mFilterMonoSamples >= kBufferSize)
			mbFilterStereo = false;
	} else {
		mFilterMonoSamples = 0;
	}

	// ---- Step 9: Mix async sources at mixing rate and send to audio tap ----
	{
		ATAudioAsyncMixInfo asyncMixInfo {};
		asyncMixInfo.mStartTime = timestamp;
		asyncMixInfo.mCount = count;
		asyncMixInfo.mNumCycles = count * kATCyclesPerSyncSample;
		asyncMixInfo.mMixingRate = mMixingRate;
		asyncMixInfo.mpLeft = mSourceBuffer[0] + mBufferLevel + kFilterOffset;
		asyncMixInfo.mpRight = mbFilterStereo ? mSourceBuffer[1] + mBufferLevel + kFilterOffset : nullptr;
		asyncMixInfo.mpMixLevels = mMixLevels;

		for(IATAudioAsyncSource *asyncSource : mAsyncAudioSources) {
			asyncSource->WriteAsyncAudio(asyncMixInfo);
		}

		if (mpAudioTap) {
			if (mbFilterStereo)
				mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, mSourceBuffer[1] + mBufferLevel + kFilterOffset, count, timestamp);
			else
				mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, nullptr, count, timestamp);
		}
	}

	mBufferLevel += count;
	VDASSERT(mBufferLevel <= kBufferSize);

	// ---- Step 10: Output filtered samples to SDL3 ----
	if (mpStream && mBufferLevel > 0) {
		// Check queue depth before pushing — drop this block if the queue is already
		// too full (e.g. turbo/fast-forward mode). This matches the Windows drop logic
		// which prevents unbounded latency growth.
		int queued = SDL_GetAudioStreamQueued(mpStream);
		int maxQueueBytes = mMixingRateInt * 2 * (int)sizeof(float)
		                    * (mLatency + mExtraBuffer + 50) / 1000;

		bool dropBlock = (queued > maxQueueBytes);

		if (dropBlock) {
			++mOverflowCount;
			++mAudioStatus.mOverflowCount;
			++mAudioStatus.mDropCount;
		} else {
			// Output the filtered buffer level worth of samples.
			// The filtered data lives at offset kFilterOffset from the start of the source buffer.
			const uint32 outputCount = mBufferLevel;
			const float *srcLeft = mSourceBuffer[0] + kFilterOffset;
			const float *srcRight = mbFilterStereo ? mSourceBuffer[1] + kFilterOffset : nullptr;

			// Interleave and push to SDL3 in chunks
			static constexpr uint32 kChunkSize = 1024;
			float interleaved[kChunkSize * 2];

			uint32 remaining = outputCount;
			uint32 offset = 0;

			while (remaining > 0) {
				uint32 n = remaining < kChunkSize ? remaining : kChunkSize;

				if (mbMute) {
					memset(interleaved, 0, n * 2 * sizeof(float));
				} else if (srcRight) {
					for (uint32 i = 0; i < n; ++i) {
						interleaved[i * 2    ] = srcLeft[offset + i];
						interleaved[i * 2 + 1] = srcRight[offset + i];
					}
				} else {
					for (uint32 i = 0; i < n; ++i) {
						interleaved[i * 2    ] = srcLeft[offset + i];
						interleaved[i * 2 + 1] = srcLeft[offset + i];
					}
				}

				SDL_PutAudioStreamData(mpStream, interleaved,
				                       (int)(n * 2 * sizeof(float)));

				offset += n;
				remaining -= n;
			}

			if (queued == 0 && count > 0) {
				++mUnderflowCount;
				++mAudioStatus.mUnderflowCount;
			}
		}
	}

	// Always shift the source buffer down and reset mBufferLevel, even if mpStream
	// is null (audio device failed to open) or we dropped the block. Without this,
	// mBufferLevel would grow without bound, causing buffer overflow and assert
	// failures on the next WriteAudio call.
	if (mBufferLevel > 0) {
		uint32 bytesToShift = sizeof(float) * kPreFilterOffset;

		memmove(mSourceBuffer[0], mSourceBuffer[0] + mBufferLevel, bytesToShift);

		if (mbFilterStereo)
			memmove(mSourceBuffer[1], mSourceBuffer[1] + mBufferLevel, bytesToShift);

		mBufferLevel = 0;
	}

	// ---- Step 11: Status and profiling ----
	if (mpStream) {
		int queued = SDL_GetAudioStreamQueued(mpStream);

		if (++mCheckCounter >= 15) {
			mCheckCounter = 0;

			mAudioStatus.mMeasuredMin = queued;
			mAudioStatus.mMeasuredMax = queued;
			mAudioStatus.mTargetMin = mLatency;
			mAudioStatus.mTargetMax = mLatency + mExtraBuffer;
			mAudioStatus.mbStereoMixing = mbFilterStereo;
			mAudioStatus.mSamplingRate = mMixingRateInt;

			// mAudioStatus.mUnderflowCount and mOverflowCount are cumulative
			// (incremented directly at detection time, never reset here).
			// The local mUnderflowCount/mOverflowCount are windowed counters
			// used only for the drop logic check.
			mUnderflowCount = 0;
			mOverflowCount = 0;
		}
	}

	if (++mProfileCounter >= 200) {
		mProfileCounter = 0;
		uint64 t = VDGetPreciseTick();

		mAudioStatus.mIncomingRate = (double)(mWritePosition - mProfileBlockStartPos) / (double)(t - mProfileBlockStartTime) * VDGetPreciseTicksPerSecond();

		mProfileBlockStartPos = mWritePosition;
		mProfileBlockStartTime = t;
	}
}

// =========================================================================
// Factory function
// =========================================================================

IATAudioOutput *ATCreateAudioOutput() {
	return new ATAudioOutputSDL3();
}
