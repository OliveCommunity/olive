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

#ifndef AUDIOWAVEFORMSYNC_H
#define AUDIOWAVEFORMSYNC_H

#include <cstdint>

#include <QVector>

#include "olive/core/render/samplebuffer.h"

namespace olive
{

class AudioWaveformSync {
public:
	struct OffsetResult {
		int64_t offset_samples = 0;
		double confidence = 0.0;
		bool valid = false;
	};

	static QVector<double>
	ExtractRmsEnvelope(const core::SampleBuffer &samples, size_t window_samples);

	static OffsetResult EstimateOffset(const core::SampleBuffer &reference,
									   const core::SampleBuffer &candidate,
									   size_t window_samples,
									   int64_t max_offset_samples);

	static OffsetResult EstimateEnvelopeOffset(const QVector<double> &reference,
											   const QVector<double> &candidate,
											   size_t window_samples,
											   int64_t max_offset_windows);
};

}

#endif // AUDIOWAVEFORMSYNC_H
