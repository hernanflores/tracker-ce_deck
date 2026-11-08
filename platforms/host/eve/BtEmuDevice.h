#ifndef HOST_BT_EMU_DEVICE_H_
#define HOST_BT_EMU_DEVICE_H_

#include "EveDevice.h"

#include <memory>
#include <string>

// Bridgetek's EVE emulator (libbt8xxemu), loaded with dlopen at run time. Never bundled or
// redistributed with this project: the user installs it, and builds with BT8XXEMU_INCLUDE_DIR
// pointing at the matching bt8xxemu.h. Returns nullptr and fills `error` when unavailable.
std::unique_ptr<EveDevice> makeBtEmuDevice(const std::string& libraryPath, std::string& error);

#endif
