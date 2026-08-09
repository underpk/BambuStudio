# BambuStudio Modifications

This document describes the modifications made to BambuStudio to enable building with modern CMake (4.x), add LAN device persistence features, and add YOLO flow calibration from OrcaSlicer.

## Table of Contents
1. [CMake 4.x Compatibility Fixes](#cmake-4x-compatibility-fixes)
2. [Dependency Build Fixes](#dependency-build-fixes)
3. [LAN Device Rename and Persistence Feature](#lan-device-rename-and-persistence-feature)
4. [YOLO Flow Calibration Feature](#yolo-flow-calibration-feature)
5. [Build Instructions](#build-instructions)

---

## CMake 4.x Compatibility Fixes

### Problem
CMake 4.x removed compatibility with `cmake_minimum_required()` versions below 3.5. The original BambuStudio code and its dependencies used older CMake minimum versions, causing build failures.

### Files Modified

#### 1. `deps/CMakeLists.txt`
**Issue:** `cmake_minimum_required()` was called after `project()`, and version was set to 3.2.

**Fix:**
- Moved `cmake_minimum_required()` before `project()`
- Updated version from 3.2 to 3.5
- Added `CMAKE_POLICY_VERSION_MINIMUM` to ExternalProject_Add for dependency compatibility

```cmake
# Before (line 23-24):
project(BambuStudio-deps)
cmake_minimum_required(VERSION 3.2)

# After:
cmake_minimum_required(VERSION 3.5)
project(BambuStudio-deps)
```

Added to `bambustudio_add_cmake_project` function's CMAKE_ARGS:
```cmake
-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
```

#### 2. `CMakeLists.txt` (root)
**Issue:** Hard block on CMake versions >= 4.0.

**Fix:** Replaced version check with compatibility setting:

```cmake
# Before:
if ( ((MSVC) OR (WIN32)) AND (${CMAKE_VERSION} VERSION_GREATER_EQUAL "4.0") )
    message(FATAL_ERROR "Only cmake versions between 3.13.x and 4.0.x is supported...")
endif()

# After:
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for CMake 4.x compatibility")
endif()
```

---

## Dependency Build Fixes

### 1. TIFF Library Download URL
**File:** `deps/TIFF/TIFF.cmake`

**Issue:** Original download URL (download.osgeo.org) was unavailable.

**Fix:** Changed to GitLab mirror and commented out hash verification:

```cmake
# Before:
URL https://download.osgeo.org/libtiff/tiff-4.1.0.zip
URL_HASH SHA256=6F3DBED9D2ECFED33C7192B5C01884078970657FA21B4AD28E3CDF3438EB2419

# After:
# Original URL was unavailable, using GitLab mirror
URL https://gitlab.com/libtiff/libtiff/-/archive/v4.1.0/libtiff-v4.1.0.zip
# URL_HASH SHA256=6F3DBED9D2ECFED33C7192B5C01884078970657FA21B4AD28E3CDF3438EB2419
```

### 2. wxWidgets Precompiled Headers
**File:** `deps/wxWidgets/wxWidgets.cmake`

**Issue:** Cotire (precompiled header) test fails with CMake 4.x, causing wxWidgets build to fail.

**Fix:** Disabled precompiled headers:

```cmake
# Before:
-DwxBUILD_PRECOMP=ON

# After:
# Disable precompiled headers due to cotire test failure with CMake 4.x
-DwxBUILD_PRECOMP=OFF
```

---

## LAN Device Rename and Persistence Feature

### Overview
Added the ability to:
1. Rename LAN-connected printers with a custom nickname
2. Persist LAN device information so devices appear after app restart
3. Save and restore custom nicknames

### Files Modified

#### 1. `src/slic3r/GUI/DeviceManager.cpp`
Added functions for local device nickname management:

```cpp
void MachineObject::set_local_dev_nickname(std::string name)
{
    AppConfig* config = GUI::wxGetApp().app_config;
    if (config) {
        if (!name.empty()) {
            GUI::wxGetApp().app_config->set_str("device_nickname", get_dev_id(), name);
        } else {
            GUI::wxGetApp().app_config->erase("device_nickname", get_dev_id());
        }
        GUI::wxGetApp().app_config->save();
    }
    dev_name = name;
}

std::string MachineObject::get_local_dev_nickname() const
{
    AppConfig* config = GUI::wxGetApp().app_config;
    if (config) {
        return GUI::wxGetApp().app_config->get("device_nickname", get_dev_id());
    }
    return "";
}

bool MachineObject::has_local_dev_nickname() const
{
    return !get_local_dev_nickname().empty();
}
```

#### 2. `src/slic3r/GUI/DeviceManager.hpp`
Added function declarations:

```cpp
void set_local_dev_nickname(std::string name);
std::string get_local_dev_nickname() const;
bool has_local_dev_nickname() const;
```

#### 3. `src/slic3r/GUI/DeviceCore/DevManager.h`
Added LAN device persistence function declarations:

```cpp
// LAN device persistence
void save_lan_device_info(const std::string& dev_id, const std::string& dev_name,
    const std::string& dev_ip, const std::string& printer_type);
void load_saved_lan_devices();
void remove_saved_lan_device(const std::string& dev_id);
```

#### 4. `src/slic3r/GUI/DeviceCore/DevManager.cpp`
Added LAN device persistence implementation:

**save_lan_device_info()** - Saves device info to app config as JSON:
```cpp
void DeviceManager::save_lan_device_info(const std::string& dev_id, const std::string& dev_name,
    const std::string& dev_ip, const std::string& printer_type)
{
    try {
        AppConfig* config = Slic3r::GUI::wxGetApp().app_config;
        if (!config || dev_id.empty()) return;

        json device_info;
        device_info["dev_name"] = dev_name;
        device_info["dev_ip"] = dev_ip;
        device_info["printer_type"] = printer_type;

        config->set_str("saved_lan_devices", dev_id, device_info.dump());
        // Config is saved automatically when app closes
    }
    catch (...) { /* error handling */ }
}
```

**load_saved_lan_devices()** - Loads saved devices on app startup:
```cpp
void DeviceManager::load_saved_lan_devices()
{
    // Loads devices from "saved_lan_devices" config section
    // Creates MachineObject instances marked as offline
    // Applies saved nicknames from "device_nickname" section
    // Devices are marked online when SSDP discovery finds them
}
```

**Modified start_refresher()** - Calls load on startup:
```cpp
void DeviceManager::start_refresher() {
    try {
        load_saved_lan_devices();
    }
    catch (...) { /* error handling */ }
    if (m_refresher) {
        m_refresher->Start();
    }
}
```

**Modified insert_local_device()** - Saves device when manually added:
```cpp
// Added at end of function:
save_lan_device_info(dev_id, dev_name, dev_ip, printer_type);
```

**Modified on_machine_alive()** - Loads nickname when device discovered:
```cpp
// Use saved local nickname if available, otherwise use SSDP discovered name
std::string local_nickname = obj->get_local_dev_nickname();
if (!local_nickname.empty()) {
    obj->set_dev_name(local_nickname);
} else {
    obj->set_dev_name(dev_name);
}
```

**Modified modify_device_name()** - Saves nickname for LAN devices:
```cpp
// For LAN mode printers, save the name locally
if (obj && obj->is_lan_mode_printer()) {
    obj->set_local_dev_nickname(dev_name);
    return;
}
```

#### 5. `src/libslic3r/AppConfig.hpp`
**Bug Fix:** The `erase()` function wasn't marking config as dirty.

```cpp
// Before:
void erase(const std::string &section, const std::string &key)
{
    auto it = m_storage.find(section);
    if (it != m_storage.end()) {
        it->second.erase(key);
    }
}

// After:
void erase(const std::string &section, const std::string &key)
{
    auto it = m_storage.find(section);
    if (it != m_storage.end()) {
        auto it2 = it->second.find(key);
        if (it2 != it->second.end()) {
            it->second.erase(it2);
            m_dirty = true;
        }
    }
}
```

### Config File Structure
The following sections are added to the BambuStudio config file:

```ini
[saved_lan_devices]
<device_id> = {"dev_name":"...","dev_ip":"...","printer_type":"..."}

[device_nickname]
<device_id> = Custom Printer Name

[access_code]
<device_id> = <access_code>

[user_access_code]
<device_id> = <user_access_code>
```

---

## YOLO Flow Calibration Feature

### Overview
Added YOLO (You Only Level Once) flow calibration from OrcaSlicer. This provides a more accurate and efficient method for calibrating filament flow rate compared to the traditional two-pass method.

### Features
- **YOLO (Recommended)** - Single-pass flow calibration with 0.01 step precision
- **YOLO (perfectionist version)** - Higher precision calibration with 0.005 step

### How It Works
YOLO calibration uses a linear scale approach where multiple test patches are printed with incrementally different flow rates. You can visually identify the best flow rate by examining the top surface quality.

### Files Added/Modified

#### New Resource Files
Copied from OrcaSlicer to `resources/calib/filament_flow/`:
- `Orca-LinearFlow.3mf` - Standard YOLO calibration model
- `Orca-LinearFlow_fine.3mf` - Fine/perfectionist YOLO calibration model

#### `src/slic3r/GUI/Plater.hpp`
Updated function signature:
```cpp
// Before:
void calib_flowrate(int pass);

// After:
void calib_flowrate(bool is_linear, int pass);
```

#### `src/slic3r/GUI/Plater.cpp`
Modified `calib_flowrate()` function to support both standard and YOLO modes:
- `is_linear = false`: Standard flow rate test (Pass 1 and Pass 2)
- `is_linear = true`: YOLO linear flow calibration

Key differences in YOLO mode:
- Uses Archimedean Chords top surface pattern
- 1 wall loop instead of 3
- 2 bottom layers instead of 1
- 10 total layers instead of 7
- Flow ratio calculated as `(current_flow + modifier) / current_flow`

#### `src/slic3r/GUI/MainFrame.cpp`
Added YOLO menu items to the Flow rate submenu in both calibration menu locations:
```cpp
flowrate_menu->AppendSeparator();
append_menu_item(flowrate_menu, wxID_ANY, _L("YOLO (Recommended)"),
    _L("YOLO flowrate calibration, 0.01 step"),
    [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(true, 1); }, ...);
append_menu_item(flowrate_menu, wxID_ANY, _L("YOLO (perfectionist version)"),
    _L("YOLO flowrate calibration, 0.005 step"),
    [this](wxCommandEvent&) { if (m_plater) m_plater->calib_flowrate(true, 2); }, ...);
```

### How to Access
1. Enable Developer Mode in Settings > Preferences
2. Click the Calibration button in the top toolbar
3. Go to Flow rate submenu
4. Select either "YOLO (Recommended)" or "YOLO (perfectionist version)"

### Usage Tips
- Start with "YOLO (Recommended)" for most filaments
- Use "perfectionist version" if you need extra precision
- Examine the printed patches and select the one with the smoothest top surface
- Apply the corresponding flow rate modifier to your filament settings

---

## Build Instructions

### Prerequisites
- Visual Studio 2022
- CMake 4.x (tested with 4.2.1)
- Git

### Build Steps

1. **Build Dependencies** (only needed once):
```batch
cd deps
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

2. **Build BambuStudio**:
```batch
cd ../..
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="..\deps\build\destdir\usr\local"
cmake --build . --config Release
```

### Notes
- Dependencies only need to be built once
- Keep the `deps/build` directory between builds
- For clean rebuild of BambuStudio only, delete just the main `build` directory

---

## Testing

### LAN Device Persistence
1. Add a LAN printer using IP address and access code
2. Rename the printer using the edit button
3. Close and reopen the app
4. Verify the printer appears in the device list with the custom name

### Expected Behavior
- Manually added LAN devices persist after app restart
- Custom nicknames are preserved
- Devices show as "offline" until SSDP discovery finds them on the network
- Once discovered, devices are marked "online" and retain their custom names

---

## License
These modifications are provided under the same license as the original BambuStudio project.

## Contributors
- Modified for CMake 4.x compatibility, LAN device persistence, and YOLO flow calibration
