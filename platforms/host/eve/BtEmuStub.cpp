#include "BtEmuDevice.h"

#ifndef HOST_HAS_BTEMU
std::unique_ptr<EveDevice> makeBtEmuDevice(const std::string&, std::string& error)
{
    error = "this build has no Bridgetek emulator support (configure with -DBT8XXEMU_INCLUDE_DIR=<dir with bt8xxemu.h>)";
    return nullptr;
}
#endif
