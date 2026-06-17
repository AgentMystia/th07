// pbg3 archive abstraction  th06 leftover include.
//
// th07 ships its data in pbg4 archives (src/pbg4/Pbg4Parser.cpp); the pbg3
// family is th06 only. These headers exist solely so the th07.hpp umbrella
// include (inherited from the th06 template) resolves. They are intentionally
// empty; no th07 module references the pbg3 types.
#pragma once
