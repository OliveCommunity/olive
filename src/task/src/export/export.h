/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_EXPORTTASK_H
#define OAK_EXPORTTASK_H

#include <map>

#include "codec/encoder.h"
#include "node/colormanager.h"
#include "node/node.h"
#include "render/copier.h"
#include "../render/render.h"

namespace olive
{

/**
 * @brief Export (encode) task over the oakcodec/oakrender C ABIs
 *
 * Takes a flat oakcodec_encoding_params POD instead of the C++
 * EncodingParams class.
 */
class ExportTask : public RenderTask {
public:
	ExportTask(OakNodeNode viewer_node, OakNodeColorManager color_manager,
			   const oakcodec_encoding_params &params);

	virtual ~ExportTask() override;

protected:
	virtual bool run() override;

	virtual bool frame_downloaded(OakCodecFrame *frame,
								  const Rational &time) override;

	virtual bool audio_downloaded(const TimeRange &range,
								  OakSampleBuffer *samples) override;

	virtual bool encode_subtitle(OakNodeBlock sub) override;

private:
	bool write_audio_loop(const TimeRange &time, OakSampleBuffer *samples);

	OakRenderProjectCopier *copier_;

	std::map<Rational, OakCodecFrame *> time_map_;

	struct TimeRangeLess {
		bool operator()(const TimeRange &a, const TimeRange &b) const
		{
			return a.in() < b.in();
		}
	};
	std::map<TimeRange, OakSampleBuffer *, TimeRangeLess> audio_map_;

	/** Owned handle (created on the copied project). */
	OakNodeColorManager color_manager_;

	oakcodec_encoding_params params_;

	OakEncoder encoder_;

	OakEncoder subtitle_encoder_;

	OakColorProcessor *color_processor_;

	int64_t frame_time_;

	int null_frame_streak_;

	Rational audio_time_;

	TimeRange export_range_;
};

}

#endif // OAK_EXPORTTASK_H
