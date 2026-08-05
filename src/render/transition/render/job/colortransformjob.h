#pragma once
// Bridge: oaknode still includes "render/job/colortransformjob.h". Route it
// to the real de-Qt oakrender header (src/render/src/job/colortransformjob.h)
// so both sides see the same class definition.
#include "../../../src/job/colortransformjob.h"
