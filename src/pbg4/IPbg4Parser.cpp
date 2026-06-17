// IPbg4Parser  interface stub for th07's pbg4 archive bitstream parser.
//
// The real IPbg4Parser class lives at FUN_0045ef00..FUN_0045fb50 (decompress,
// archive open, entry-table build). Those are large bitstream codecs that are
// not yet ported. This file provides an empty translation unit so configure.py
// can produce IPbg4Parser.obj for the link step; real bodies land when the
// codec is lifted from the orig binary.
#include "diffbuild.hpp"
