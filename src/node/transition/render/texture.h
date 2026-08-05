#pragma once
// Bridge: routed to the real de-Qt oakrender header (src/render/src/texture.h,
// M7) so oaknode and oakrender see the same class definition.
// samplebuffer.h is included first because oaknode's value.h relied on the
// old transition texture.h pulling it in transitively.
#include "render/samplebuffer.h"
#include "../../../render/src/texture.h"
