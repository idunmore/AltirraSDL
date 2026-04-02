# Verification: Devices Dialog

Compares Windows `uidevices.cpp` device management dialog with SDL3
`ui_system.cpp` RenderDevicesCategory.

---

## Handler: ATUIControllerDevices::Add (Add Device)
**Win source:** uidevices.cpp:972
**Win engine calls:**
1. Determine parent device/bus from tree selection
2. Filter device categories based on bus/parent/base categories
3. Show `ATUIDialogDeviceNew` tree dialog with categorized device list + RTF help text
4. On OK, retrieve device definition via `GetDeviceDefinition(tag)`
5. Load last-used properties from `VDRegistryAppKey("Device config history")`
6. Call device-specific configure function (`GetDeviceConfigureFn`) if available
7. Check `kATDeviceDefFlag_RebootOnPlug`, confirm reboot if needed
8. `devMgr.AddDevice(def, props, hasParent)` to create device
9. Attach to parent bus if applicable (`devBus->AddChildDevice`)
10. Save device config history via `SaveSettings()`
11. Refresh tree, select new device node
**SDL3 source:** ui_system.cpp:1445 -- "Configure System > Devices > Add Device..."
**SDL3 engine calls:**
1. Open popup menu with flat category list (kDeviceCategories)
2. For each category, show submenu of device entries
3. Check device definition exists via `GetDeviceDefinition(tag)`
4. On click: `devMgr->AddDevice(tag, emptyPropertySet)`
**Status:** DIVERGE
**Notes:**
- SDL3 does not show device-specific configuration dialog before adding
- SDL3 does not load/save device config history from registry
- SDL3 does not filter categories based on parent device/bus context
- SDL3 does not support hierarchical parent/child device relationships (no bus selection)
- SDL3 does not check kATDeviceDefFlag_RebootOnPlug or confirm reboot
- SDL3 does not show RTF help text descriptions for devices
- SDL3 passes empty ATPropertySet instead of loading saved/configured properties
**Severity:** Critical

---

## Handler: ATUIControllerDevices::Remove (Remove Device)
**Win source:** uidevices.cpp:1147
**Win engine calls:**
1. Validate device node is removable (`mbCanRemove`)
2. `devMgr.MarkAndSweep()` to find dependent child devices
3. Check `kATDeviceDefFlag_RebootOnPlug` on device and children
4. Show confirmation dialog listing child devices to be removed
5. Detach from parent bus (`RemoveChildDevice`)
6. `devMgr.RemoveDevice(dev)` for device
7. `devMgr.RemoveDevice(child)` for each orphaned child
8. If reboot needed, `ATUIConfirmResetComplete()`
9. Refresh tree, re-select appropriate node
**SDL3 source:** ui_system.cpp:1430 -- "Configure System > Devices > Remove button"
**SDL3 engine calls:**
1. `devMgr->RemoveDevice(dev.pDev)`
2. Reset selection index
**Status:** DIVERGE
**Notes:**
- SDL3 does not check for dependent child devices (MarkAndSweep)
- SDL3 does not confirm removal when child devices would be orphaned
- SDL3 does not check kATDeviceDefFlag_RebootOnPlug or trigger reboot
- SDL3 has two remove paths: per-row "Remove" button and bottom "Remove" button
**Severity:** Medium

---

## Handler: ATUIControllerDevices::RemoveAll
**Win source:** uidevices.cpp:1251
**Win engine calls:**
1. Check if any device has kATDeviceDefFlag_RebootOnPlug
2. Show confirmation dialog with reboot warning if needed
3. `devMgr.RemoveAllDevices(false)`
4. If reboot needed, `ATUIConfirmResetComplete()`
5. Refresh tree
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** SDL3 has no "Remove All" button or equivalent.
**Severity:** Low

---

## Handler: ATUIControllerDevices::Settings (Device Settings)
**Win source:** uidevices.cpp:1286
**Win engine calls:**
1. Get device info, check `mpConfigTag` exists
2. Get device-specific configure function via `GetDeviceConfigureFn(configTag)`
3. Get current device settings via `dev->GetSettings(pset)`
4. Call configure function with current property set (shows dialog)
5. If accepted: `devMgr.ReconfigureDevice(*dev, pset)`
6. Save settings to registry via `SaveSettings(configTag, pset)`
7. Refresh tree
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** SDL3 has no Settings button or device configuration dialog. Devices are added with default/empty properties and cannot be reconfigured after creation.
**Severity:** Critical

---

## Handler: ATUIControllerDevices::More (Context Menu)
**Win source:** uidevices.cpp:1323
**Win engine calls:**
1. Get selected device context
2. Display context menu with device-specific extended commands (XCmd)
3. Includes Copy/Paste device tree functionality
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** SDL3 has no context menu on devices, no copy/paste device tree support.
**Severity:** Low

---

## Handler: ATUIControllerDevices::Copy / Paste
**Win source:** uidevices.cpp:1332 / 1340
**Win engine calls:**
1. Copy: `ExecuteXCmd(dev, busIndex, ATGetDeviceXCmdCopyWithChildren())`
2. Paste: `ExecuteXCmd(dev, busIndex, ATGetDeviceXCmdPaste())`
**SDL3 source:** N/A
**SDL3 engine calls:** N/A
**Status:** MISSING
**Notes:** Device clipboard operations not implemented.
**Severity:** Low

---

## Handler: Device Tree Display
**Win source:** uidevices.cpp:796-854, 949-965
**Win engine calls:**
1. Hierarchical tree view with Computer as root
2. Devices shown with sub-devices (parent/child buses)
3. Settings blurb shown inline (`dev->GetSettingsBlurb()`)
4. Firmware status shown via icons (OK, Missing, Invalid)
5. Error nodes displayed for device issues
**SDL3 source:** ui_system.cpp:1345-1480 -- "Configure System > Devices"
**SDL3 engine calls:**
1. Flat table (not tree) listing all external devices
2. Firmware status shown as colored text
3. Per-row "Remove" button
4. Add Device popup with categories
**Status:** DIVERGE
**Notes:**
- SDL3 uses flat list instead of hierarchical tree (no parent/child relationships shown)
- SDL3 does not show device settings blurb inline
- SDL3 does not show device error nodes
- SDL3 does show firmware status (OK/Missing/Invalid) which is good
**Severity:** Medium

---

## Summary

| Feature | Status |
|---------|--------|
| Add Device (basic) | Partial -- adds but no config dialog |
| Add Device (with properties/config) | MISSING |
| Remove Device | Partial -- no child device handling |
| Remove All | MISSING |
| Settings/Configure | MISSING |
| Context Menu / More | MISSING |
| Copy/Paste Devices | MISSING |
| Hierarchical Tree | MISSING (flat list) |
| Firmware Status | MATCH |
| Device Descriptions | MISSING |
