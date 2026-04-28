//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_PIA_H
#define f_AT_PIA_H

#include <vd2/system/function.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicepia.h>

class ATScheduler;
class ATSaveStateReader;
class ATScheduler;
struct ATTraceContext;
class ATTraceChannelFormatted;
class IATObjectState;

struct ATPIAState {
	uint8 mORA;
	uint8 mDDRA;
	uint8 mCRA;
	uint8 mORB;
	uint8 mDDRB;
	uint8 mCRB;
};

enum ATPIAOutputBits {
	kATPIAOutput_CA2 = 0x10000,		// motor
	kATPIAOutput_CB2 = 0x20000		// command
};

struct ATPIAFloatingInputs {
	ATScheduler *mpScheduler;
	uint8 mFloatingInputMask;
	uint32	mRandomSeed;
	uint32	mDecayTimeMin;
	uint32	mDecayTimeRange;
	uint64	mFloatTimers[8];

	// When true, suppress the absolute-scheduler-tick mix into
	// mRandomSeed in ChangePortDirectionInput.  Set by netplay at
	// lockstep entry: both peers cold-boot at different absolute
	// scheduler tick offsets (each emulator process has its own pre-
	// session warm-up), so XORing GetTick64() into the LFSR seed
	// when the OS writes port direction registers during boot
	// produces divergent floating-input bits on subsequent PIA reads
	// — the bits feed the joystick/console-key registers, so CPU
	// state diverges within a few frames and lockstep desyncs.  The
	// LFSR self-step on the next line still mixes its own state, so
	// suppressing only the wallclock-entropy add keeps the RNG
	// quality usable for in-session play while making every peer's
	// sequence reproducible from the locked netplay seed.  Default
	// false preserves original behaviour for non-netplay use.
	bool mbDeterministicSeedMix = false;
};

class ATPIAEmulator final : public IATDevicePIA {
	friend class ATTest_Emu_PIA;

	ATPIAEmulator(const ATPIAEmulator&) = delete;
	ATPIAEmulator& operator=(const ATPIAEmulator&) = delete;
public:
	enum : uint32 { kTypeID = "PIA "_vdfcctypeid };

	ATPIAEmulator();
	~ATPIAEmulator();

	uint8 GetPortAOutput() const { return (uint8)mOutput; }
	uint8 GetPortBOutput() const { return (uint8)(mOutput >> 8); }

	int AllocInput() override;
	void FreeInput(int index) override;
	void SetInput(int index, uint32 rval) override;
	void SetInputBits(int index, uint32 rval, uint32 mask);

	uint32 RegisterDynamicInput(bool portb, vdfunction<uint8()> fn) override;
	void UnregisterDynamicInput(bool portb, uint32 token) override;

	uint32 GetOutputState() const override { return mOutput; }
	int AllocOutput(ATPIAOutputFn fn, void *ptr, uint32 changeMask) override;
	void ModifyOutputMask(int index, uint32 changeMask) override;
	void FreeOutput(int index) override;

	void SetTraceContext(ATTraceContext *context);
	void SetIRQHandler(vdfunction<void(uint32, bool)> fn);

	void Init(ATScheduler *scheduler);
	void ColdReset();
	void WarmReset();

	void SetCA1(bool level);		// Proceed
	void SetCA2(bool level);
	void SetCB1(bool level);		// Interrupt

	uint8 DebugReadByte(uint8 addr) const;
	uint8 ReadByte(uint8 addr);
	void WriteByte(uint8 addr, uint8 value);

	// Sets which port B bits are left floating and can therefore drift when switched
	// to input mode. Port A cannot float since it has internal pull-ups.
	//
	// The scheduler pointer, input mask, and decay time range, and random fields must
	// be filled out. The float timers will be auto-inited on set.
	//
	// Floating bits are only visible to the CPU, not to allocated outputs. Any bits
	// that are actually used are typically pulled up. A special case is U1MB, which
	// can have active bank bits that are floating on the PIA. We don't use floating
	// bits for banking there either, as the U1MB is itself shadowing PIA state
	// independently in the CPLD and doesn't see the floating bits.
	//
	void SetPortBFloatingInputs(ATPIAFloatingInputs *inputs);

	// Zero all port-B floating-input decay timers.  The deadlines stored
	// in mFloatTimers are absolute scheduler ticks — captured into a
	// savestate they make sense only on the simulator that captured
	// them.  PostLoadState already zeros them on the snapshot-loading
	// peer, but the snapshot-CAPTURING peer keeps its absolute
	// deadlines.  For deterministic netplay both peers must reach
	// lockstep with the same float-timer state; calling this on the
	// host at lockstep entry brings it in line with the joiner's
	// post-load state.  After both peers are zeroed, future
	// SetPortBDirection() calls re-arm the timers using each peer's
	// local t64 plus a deterministic decay (locked RNG seed), so the
	// relative tick at which each bit decays matches across peers.
	void ResetFloatingInputTimers();

	void GetState(ATPIAState& state) const;
	void DumpState();

	void BeginLoadState(ATSaveStateReader& reader);
	void LoadStateArch(ATSaveStateReader& reader);
	void EndLoadState(ATSaveStateReader& reader);

	void SaveState(IATObjectState **pp) const;
	void LoadState(const IATObjectState *state);
	void LoadState(const class ATSaveStatePia& state);
	void PostLoadState();

private:
	struct CountedMask32 {
		struct Impl;

		alignas(16) uint8 mCounts[32] {};

		void AddZeroBits(uint32 mask);
		void RemoveZeroBits(uint32 mask);

		uint32 ReadZeroMask() const;
	};

	struct OutputEntry {
		uint32 mChangeMask = 0;
		ATPIAOutputFn mpFn = nullptr;
		void *mpData = nullptr;
	};

	uint32& GetInput(unsigned slot);
	OutputEntry& GetOutput(unsigned slot);

	void UpdateCA2();
	void UpdateCB2();
	void UpdateOutput();
	void RecomputeOutputReportMask();
	bool SetPortBDirection(uint8 value);

	void NegateIRQs(uint32 mask);
	void AssertIRQs(uint32 mask);

	void SetCRA(uint8 v);
	void SetCRB(uint8 v);

	void UpdateTraceCRA();
	void UpdateTraceCRB();
	void UpdateTraceInputA();

	uint8 ComputePortAInput() const;
	uint8 ComputePortBInput() const;

	uint8 ReadDynamicInputs(bool portb) const;

	ATScheduler *mpScheduler;
	ATPIAFloatingInputs *mpFloatingInputs;
	vdfunction<void(uint32, bool)> mpIRQHandler;

	CountedMask32 mInputState;

	uint32	mInput;
	uint32	mOutput;

	uint32	mPortOutput;
	uint32	mPortDirection;

	bool	mbHasDynamicAInputs = false;
	bool	mbHasDynamicBInputs = false;

	uint8	mPORTACTL;
	uint8	mPORTBCTL;
	bool	mbPIAEdgeA;
	bool	mbPIAEdgeB;
	bool	mbCA1;
	bool	mbCA2;
	bool	mbCB1;
	uint8	mPIACB2;

	enum {
		kPIACS_Floating,
		kPIACS_Low,
		kPIACS_High
	};

	uint32	mOutputReportMask;
	uint32	mInputAllocBitmap[8] {};
	uint32	mIntInputs[8];
	uint32	*mpExtInputs = nullptr;

	OutputEntry mIntOutputs[4];
	vdfastvector<OutputEntry> mExtOutputs;
	uint32 mOutputNextAllocIndex = 0;

	vdvector<vdfunction<uint8()>> mDynamicInputs[2];
	vdfastvector<uint32> mDynamicInputTokens[2];
	uint32 mDynamicTokenCounter = 0;

	ATTraceContext *mpTraceContext = nullptr;
	ATTraceChannelFormatted *mpTraceCRA = nullptr;
	ATTraceChannelFormatted *mpTraceCRB = nullptr;
	ATTraceChannelFormatted *mpTraceInputA = nullptr;
};

#endif
