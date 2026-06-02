#pragma once
#include "IDiscovery.h"
#include <memory>

namespace BetterSend {

// Factory: returns a heap-allocated MdnsDiscovery wrapped as IDiscovery.
// Defined in MdnsDiscovery.cpp — callers need not know the concrete class.
std::unique_ptr<IDiscovery> makeDiscovery();

} // namespace BetterSend
