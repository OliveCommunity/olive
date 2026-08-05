#pragma once
// Transitional: engine/common/current.h was sunk to oakcommon (current.h,
// type-erased de-Qt Current). Forward so existing "common/current.h"
// includes keep working. M7/M9 retarget callers to the real boundary.
// Angle brackets: a quoted "current.h" here would resolve to this file
// itself (same directory).
#include <current.h>
