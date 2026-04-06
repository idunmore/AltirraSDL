//	AltirraSDL - Dear ImGui Performance Analyzer (Trace Viewer)
//	Shared context and declarations for the multi-file trace viewer.

#ifndef f_UI_DBG_TRACEVIEWER_H
#define f_UI_DBG_TRACEVIEWER_H

#include <atomic>
#include <vector>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include "trace.h"
#include "tracecpu.h"
#include "profiler.h"

// =========================================================================
// Shared context passed between trace viewer sub-renderers
// =========================================================================

struct ATImGuiTraceViewerContext {
	// Current trace collection (may be null)
	vdrefptr<ATTraceCollection> mpCollection;

	// ---- Zoom / pan state ----
	double mStartTime = 0;
	double mSecondsPerPixel = 0.001;
	sint32 mZoomLevel = -15;		// pow(10, mZoomLevel / 5.0)
	double mTraceDuration = 0;

	// ---- Selection ----
	bool mbSelectionMode = false;	// true = select mode, false = move mode
	bool mbSelectionValid = false;
	double mSelectStart = 0;
	double mSelectEnd = 0;

	// ---- Recording ----
	bool mbRecording = false;
	std::atomic<bool> mbTraceLimitReached{false}; // set from emulation thread
	ATTraceSettings mSettings {};

	// ---- Focus time (for synchronizing timeline ↔ panels) ----
	double mFocusTime = -1;
	bool mbFocusTimeChanged = false;

	// ---- Timestamp decoder ----
	ATCPUTimestampDecoder mTimestampDecoder {};

	// ---- Channel layout (rebuilt when collection changes) ----
	enum ChannelType {
		kChannelType_Default,
		kChannelType_Video,
		kChannelType_Tape,
		kChannelType_Log
	};

	struct ChannelView {
		vdrefptr<IATTraceChannel> mpChannel;
		VDStringW mName;
		ChannelType mType = kChannelType_Default;
	};

	struct GroupView {
		VDStringW mName;
		std::vector<ChannelView> mChannels;
		bool mbVariableHeight = false;
	};

	std::vector<GroupView> mGroups;

	// ---- Special channels extracted during rebuild ----
	vdrefptr<IATTraceChannel> mpFrameChannel;
	vdrefptr<ATTraceChannelCPUHistory> mpCPUHistoryChannel;
	vdrefptr<IATTraceChannel> mpLogChannel;

	// ---- Vertical scroll (shared between labels and events) ----
	float mScrollY = 0;

	// ---- Draggable splitter width ----
	float mLabelWidth = 150.0f;

	// ---- Generation counter (bumped on collection change) ----
	uint32 mCollectionGeneration = 0;

	// ---- Trace limit event callback ----
	uint32 mSimEventIdTraceLimited = 0;

	// ---- Profile options (shared with panels) ----
	ATProfileCounterMode mProfileCounterModes[2] = { kATProfileCounterMode_None, kATProfileCounterMode_None };
	bool mbGlobalAddressesEnabled = false;

	// ---- Zoom helper ----
	void ZoomDeltaSteps(double centerTime, sint32 steps, float viewWidthPixels);
};

// =========================================================================
// Sub-renderer functions (implemented in separate .cpp files)
// =========================================================================

void ATImGuiTraceViewer_RenderTimeline(ATImGuiTraceViewerContext& ctx);
void ATImGuiTraceViewer_RenderPanels(ATImGuiTraceViewerContext& ctx);
void ATImGuiTraceViewer_ResetPanelState();

// Callback: request the pane to enable CPU history and restart tracing
using ATImGuiTraceViewerEnableCPUHistoryFn = void(*)();
void ATImGuiTraceViewer_SetEnableCPUHistoryCallback(ATImGuiTraceViewerEnableCPUHistoryFn fn);

#endif // f_UI_DBG_TRACEVIEWER_H
