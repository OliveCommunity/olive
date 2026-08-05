#pragma once
// Bridge: oaknode still includes "render/texture.h". Route it to the real
// de-Qt oakrender header (src/render/src/texture.h) so both sides see the
// same class definition. samplebuffer.h is included first because oaknode's
// value.h relied on the old transition texture.h pulling it in transitively.
#include "render/samplebuffer.h"
#include "../../src/texture.h"
