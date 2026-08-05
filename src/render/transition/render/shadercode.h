#pragma once
// Bridge: oaknode still includes "render/shadercode.h". Route it to the
// real de-Qt oakrender header (src/render/src/shadercode.h) so both sides
// see the same class definition.
#include "../../src/shadercode.h"
