/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_OTIOUTILS_H
#define OAK_OTIOUTILS_H

#ifdef USE_OTIO
#include <opentimelineio/version.h>
// OTIO >= 0.18 splits the version triple (OPENTIMELINEIO_VERSION, e.g.
// v0_19_0) from the actual C++ namespace (OPENTIMELINEIO_VERSION_NS, e.g.
// v0_19). Older releases (0.16) only have the former.
#if defined(OPENTIMELINEIO_VERSION_NS)
namespace OTIO = opentimelineio::OPENTIMELINEIO_VERSION_NS;
#else
namespace OTIO = opentimelineio::OPENTIMELINEIO_VERSION;
#endif
#endif

#endif // OTIOUTILS
