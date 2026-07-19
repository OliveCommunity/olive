/*
 * Olive Community Edition - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_LIBOLIVECORE_H
#define OAK_LIBOLIVECORE_H

#include "render/audioparams.h"
#include "render/pixelformat.h"
#include "render/samplebuffer.h"
#include "render/sampleformat.h"
#include "util/bezier.h"
#include "util/color.h"
#include "util/cpuoptimize.h"
#include "util/log.h"
#include "util/math.h"
#include "util/rational.h"
#include "util/stringutils.h"
#include "util/tests.h"
#include "util/timecodefunctions.h"
#include "util/timerange.h"
// util/value.h is deliberately not part of the public API: the generic
// Value container is unused by consumers and stays internal to the library

#endif // OAK_LIBOLIVECORE_H
