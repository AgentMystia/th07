// FileAbstraction — stub for th07's file-abstraction layer.
//
// In th06 this wrapped raw FILE*/HANDLE access behind a uniform interface.
// th07's version lives at FUN_00447670.. (RawOpen/RawRead/RawWrite/RawClose,
// already partially mirrored by FileSystem.cpp). Not yet ported as a class.
// Empty translation unit so configure.py can produce FileAbstraction.obj for
// the link step.
#include "diffbuild.hpp"
