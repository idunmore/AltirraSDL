# Verification: Audio Handlers

Cross-reference of Windows audio command handlers against SDL3 implementation.

**Windows source:** `src/Altirra/source/cmdaudio.cpp`
**SDL3 source:** `src/AltirraSDL/source/ui_system.cpp` (Audio category, lines 880-953)

---

## Handler: OnCommandAudioToggleStereo
**Win source:** cmdaudio.cpp:30-32
**Win engine calls:**
1. `g_sim.SetDualPokeysEnabled(!g_sim.IsDualPokeysEnabled())`
**SDL3 source:** ui_system.cpp:890-892 -- "Config > Audio > Stereo"
**SDL3 engine calls:**
1. `sim.SetDualPokeysEnabled(dualPokey)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioToggleMonitor
**Win source:** cmdaudio.cpp:34-36
**Win engine calls:**
1. `g_sim.SetAudioMonitorEnabled(!g_sim.IsAudioMonitorEnabled())`
**SDL3 source:** ui_system.cpp:942-944 -- "Config > Audio > Audio monitor"
**SDL3 engine calls:**
1. `sim.SetAudioMonitorEnabled(audioMonitor)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioToggleScope
**Win source:** cmdaudio.cpp:38-40
**Win engine calls:**
1. `g_sim.SetAudioScopeEnabled(!g_sim.IsAudioScopeEnabled())`
**SDL3 source:** ui_system.cpp:946-948 -- "Config > Audio > Audio scope"
**SDL3 engine calls:**
1. `sim.SetAudioScopeEnabled(audioScope)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioToggleMute
**Win source:** cmdaudio.cpp:42-47
**Win engine calls:**
1. `g_sim.GetAudioOutput()` -> `out->SetMute(!out->GetMute())`
**SDL3 source:** ui_system.cpp:883-888 -- "Config > Audio > Mute All"
**SDL3 engine calls:**
1. `sim.GetAudioOutput()` -> `pAudio->SetMute(muted)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioOptionsDialog
**Win source:** cmdaudio.cpp:49-51
**Win engine calls:**
1. `ATUIShowAudioOptionsDialog(ATUIGetNewPopupOwner())`
**SDL3 source:** ui_system.cpp:951-952 -- "Config > Audio > Host audio options..."
**SDL3 engine calls:**
1. `state.showAudioOptions = true` (opens a separate audio options dialog)
**Status:** MATCH
**Notes:** SDL3 opens a separate host audio options dialog window. The exact contents of that dialog would need separate verification.
**Severity:** N/A

---

## Handler: OnCommandAudioToggleNonlinearMixing
**Win source:** cmdaudio.cpp:53-57
**Win engine calls:**
1. `pokey.SetNonlinearMixingEnabled(!pokey.IsNonlinearMixingEnabled())`
**SDL3 source:** ui_system.cpp:900-902 -- "Config > Audio > Non-linear mixing"
**SDL3 engine calls:**
1. `pokey.SetNonlinearMixingEnabled(nonlinear)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioToggleSpeakerFilter
**Win source:** cmdaudio.cpp:59-63
**Win engine calls:**
1. `pokey.SetSpeakerFilterEnabled(!pokey.IsSpeakerFilterEnabled())`
**SDL3 source:** ui_system.cpp:908-910 -- "Config > Audio > Simulate console speaker"
**SDL3 engine calls:**
1. `pokey.SetSpeakerFilterEnabled(speaker)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioToggleSerialNoise
**Win source:** cmdaudio.cpp:65-69
**Win engine calls:**
1. `pokey.SetSerialNoiseEnabled(!pokey.IsSerialNoiseEnabled())`
**SDL3 source:** ui_system.cpp:904-906 -- "Config > Audio > Serial noise"
**SDL3 engine calls:**
1. `pokey.SetSerialNoiseEnabled(serialNoise)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioToggleChannel (channels 0-3)
**Win source:** cmdaudio.cpp:71-74
**Win engine calls:**
1. `pokey.SetChannelEnabled(channel, !pokey.IsChannelEnabled(channel))`
**SDL3 source:** ui_system.cpp:914-922 -- "Config > Audio > Enabled channels > 1/2/3/4"
**SDL3 engine calls:**
1. `pokey.SetChannelEnabled(i, ch)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: OnCommandAudioToggleSecondaryChannel (channels 0-3)
**Win source:** cmdaudio.cpp:76-79
**Win engine calls:**
1. `pokey.SetSecondaryChannelEnabled(channel, !pokey.IsSecondaryChannelEnabled(channel))`
**SDL3 source:** ui_system.cpp:925-933 -- "Config > Audio > Enabled channels > 1R/2R/3R/4R" (shown only when stereo enabled)
**SDL3 engine calls:**
1. `pokey.SetSecondaryChannelEnabled(i, ch)`
**Status:** MATCH
**Severity:** N/A

---

## Handler: Audio.ToggleStereoAsMono
**Win source:** cmdaudio.cpp:95-103
**Win engine calls:**
1. `pokey.SetStereoAsMonoEnabled(!pokey.IsStereoAsMonoEnabled())`
**Win enable check:** `g_sim.IsDualPokeysEnabled()`
**SDL3 source:** ui_system.cpp:896-898 -- "Config > Audio > Downmix stereo to mono"
**SDL3 engine calls:**
1. `pokey.SetStereoAsMonoEnabled(stereoMono)`
**Status:** DIVERGE
**Notes:** SDL3 checkbox is always visible regardless of whether stereo is enabled. Windows only enables this command when dual POKEYs are active (`g_sim.IsDualPokeysEnabled()` is the enable check). The SDL3 version should gray out or hide this checkbox when stereo is off.
**Severity:** Low

---

## Handler: OnCommandAudioToggleSlightSid
**Win source:** cmdaudio.cpp:81-85
**Win engine calls:**
1. `dm.ToggleDevice("slightsid")`
**SDL3 source:** N/A (no dedicated audio toggle, but available through Config > Devices)
**SDL3 engine calls:** N/A
**Status:** MATCH (available via Devices category)
**Notes:** SlightSID is a device and is available through the Devices configuration category.
**Severity:** N/A

---

## Handler: OnCommandAudioToggleCovox
**Win source:** cmdaudio.cpp:87-91
**Win engine calls:**
1. `dm.ToggleDevice("covox")`
**SDL3 source:** N/A (no dedicated audio toggle, but available through Config > Devices)
**SDL3 engine calls:** N/A
**Status:** MATCH (available via Devices category)
**Notes:** Covox is a device and is available through the Devices configuration category.
**Severity:** N/A

---

## Additional SDL3-only feature: Drive Sounds
**Win source:** N/A (exists in Windows via separate path)
**SDL3 source:** ui_system.cpp:938-940 -- "Config > Audio > Drive Sounds"
**SDL3 engine calls:**
1. `ATUISetDriveSoundsEnabled(driveSounds)`
**Status:** MATCH (present in both, SDL3 puts it in Audio category)
**Severity:** N/A

---

# Summary

| Status   | Count | Items |
|----------|-------|-------|
| MATCH    | 12    | Stereo, audio monitor, audio scope, mute, audio options, nonlinear mixing, speaker filter, serial noise, channel 1-4, secondary channel 1-4, SlightSID (via Devices), Covox (via Devices), drive sounds |
| DIVERGE  | 1     | Stereo-as-mono (missing enable check for dual POKEY) |
| MISSING  | 0     | -- |

**Critical items:** None.

**Low priority:** Stereo-as-mono checkbox should be disabled/hidden when dual POKEYs are not active.
