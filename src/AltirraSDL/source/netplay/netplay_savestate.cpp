// Altirra SDL3 netplay - mid-session savestate capture / apply (impl)

#include <stdafx.h>

#include "netplay_savestate.h"

#include "simulator.h"

#include <vd2/system/file.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/zip.h>
#include <vd2/system/refcount.h>

#include <at/atcore/serialization.h>

#include "savestateio.h"  // from Altirra/h

#include "netplay_profile.h"

#include <at/atcore/logging.h>

extern ATSimulator g_sim;
extern ATLogChannel g_ATLCNetplay;

namespace ATNetplay {

bool CaptureSavestate(std::vector<uint8_t>& out) {
	out.clear();
	try {
		vdrefptr<IATSerializable> snapshot;
		vdrefptr<IATSerializable> snapshotInfo;
		g_sim.CreateSnapshot(~snapshot, ~snapshotInfo);
		if (!snapshot) return false;

		VDMemoryBufferStream mbs;
		VDBufferedWriteStream bws(&mbs, 65536);
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(bws));
		{
			vdautoptr<IATSaveStateSerializer> ser(
				ATCreateSaveStateSerializer(L"savestate.json"));
			ser->Serialize(*zip, *snapshot);
		}
		zip->Finalize();
		bws.Flush();

		auto span = mbs.GetBuffer();
		out.assign(span.begin(), span.end());
		g_ATLCNetplay("resync: captured savestate (%zu bytes)", out.size());
		return !out.empty();
	} catch (const MyError& e) {
		g_ATLCNetplay("resync: capture failed: %s", e.c_str());
		out.clear();
		return false;
	} catch (...) {
		g_ATLCNetplay("resync: capture failed (unknown exception)");
		out.clear();
		return false;
	}
}

bool ApplySavestate(const uint8_t* data, size_t len) {
	if (!data || len == 0) return false;
	try {
		VDMemoryStream ms(data, (uint32_t)len);
		VDZipArchive zip;
		zip.Init(&ms);

		vdautoptr<IATSaveStateDeserializer> ds(
			ATCreateSaveStateDeserializer(L"savestate.json"));
		vdrefptr<IATSerializable> root;
		ds->Deserialize(zip, ~root);
		if (!root) {
			g_ATLCNetplay("resync: deserialize returned null snapshot");
			return false;
		}
		if (!g_sim.ApplySnapshot(*root, nullptr)) {
			g_ATLCNetplay("resync: ApplySnapshot refused the payload");
			return false;
		}

		// Re-normalise the RNG state that savestates don't round-trip.
		// Both peers hit this line with the same constant so their PIA
		// floating-input LFSR streams realign from this frame forward.
		// See netplay_glue.cpp:176-214 for the full background.
		g_sim.ReseedNetplayRandomState(ATNetplayProfile::kLockedRandomSeed);
		g_ATLCNetplay("resync: applied savestate (%zu bytes, RNG reseeded)", len);
		return true;
	} catch (const MyError& e) {
		g_ATLCNetplay("resync: apply failed: %s", e.c_str());
		return false;
	} catch (...) {
		g_ATLCNetplay("resync: apply failed (unknown exception)");
		return false;
	}
}

} // namespace ATNetplay
