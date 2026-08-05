#pragma once
// Transitional: SampleBuffer sunk to oakcore (olive::core::SampleBuffer, C ABI
// wrapper). Re-exported into namespace olive for node code. M7 will retarget
// this include to the real oakrender boundary.
#include "olive/core/render/samplebuffer.h"
namespace olive { using core::SampleBuffer; }
