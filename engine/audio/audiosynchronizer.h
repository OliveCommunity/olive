/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_AUDIOSYNCHRONIZER_H
#define OAK_AUDIOSYNCHRONIZER_H

#include <cstdint>

#include "olive/core/util/rational.h"

namespace olive
{

class AudioSynchronizer {
public:
	struct SourceClip {
		core::Rational source_start_time;
		core::Rational media_in;
		bool has_source_start_time = false;
	};

	struct Placement {
		core::Rational timeline_in;
		bool valid = false;
	};

	static Placement
	place_by_source_time(const SourceClip &reference, const SourceClip &candidate,
					  const core::Rational &reference_timeline_in);

	static Placement
	place_by_waveform_offset(const core::Rational &reference_timeline_in,
						  int64_t candidate_offset_samples, int sample_rate);
};

}

#endif // OAK_AUDIOSYNCHRONIZER_H
