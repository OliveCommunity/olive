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

#include "codec/encoder.h"
#include "node/output/viewer/viewer.h"
#include "render/colorprocessor.h"
#include "render/projectcopier.h"
#include "task/render/render.h"
#include "task/task.h"

namespace olive
{

class ExportTask : public RenderTask {
	Q_OBJECT
public:
	ExportTask(ViewerOutput *viewer_node, ColorManager *color_manager,
			   const EncodingParams &params);

protected:
	virtual bool run() override;

	virtual bool frame_downloaded(FramePtr frame, const Rational &time) override;

	virtual bool audio_downloaded(const TimeRange &range,
								 const SampleBuffer &samples) override;

	virtual bool encode_subtitle(const SubtitleBlock *sub) override;

	virtual bool two_step_frame_rendering() const override
	{
		return false;
	}

private:
	bool write_audio_loop(const TimeRange &time, const SampleBuffer &samples);

	ProjectCopier *copier_;

	QHash<Rational, FramePtr> time_map_;

	QHash<TimeRange, SampleBuffer> audio_map_;

	ColorManager *color_manager_;

	EncodingParams params_;

	std::shared_ptr<Encoder> encoder_;

	std::shared_ptr<Encoder> subtitle_encoder_;

	ColorProcessorPtr color_processor_;

	int64_t frame_time_;

	Rational audio_time_;

	TimeRange export_range_;
};

}

#endif // OAK_EXPORTTASK_H
