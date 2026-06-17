// EclManager module stub for th07 (Perfect Cherry Blossom).
//
// Reverse-engineering of EclManager is not yet started. This header provides the
// minimal namespace + forward declarations needed by th07.hpp's include chain
// and by other modules that reference EclManager. Bodies live in EclManager.cpp as
// no-op stubs until the module is lifted from the orig binary.
#pragma once

#include "ZunResult.hpp"
#include "inttypes.hpp"

namespace th07
{
namespace EclManager
{
ZunResult RegisterChain();
}
}
