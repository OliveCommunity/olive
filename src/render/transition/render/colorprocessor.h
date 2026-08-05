#pragma once
// Bridge: oaknode still includes "render/colorprocessor.h". Route it to the
// real de-Qt oakrender header (src/render/src/colorprocessor.h) so both sides
// see the same class definition.
#include "../../src/colorprocessor.h"
