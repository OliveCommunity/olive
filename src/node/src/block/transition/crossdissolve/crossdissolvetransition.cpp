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

#include "crossdissolvetransition.h"

#include "filefunctions.h"

namespace olive
{

CrossDissolveTransition::CrossDissolveTransition()
{
}

std::string CrossDissolveTransition::name() const
{
	return "Cross Dissolve";
}

std::string CrossDissolveTransition::id() const
{
	return "org.olivevideoeditor.Olive.crossdissolve";
}

std::vector<Node::CategoryID> CrossDissolveTransition::category() const
{
	return { k_category_transition };
}

std::string CrossDissolveTransition::description() const
{
	return "Smoothly transition between two clips.";
}

ShaderCode
CrossDissolveTransition::get_shader_code(const ShaderRequest &request) const
{
	(void) request;

	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/crossdissolve.frag"),
		std::string());
}

void CrossDissolveTransition::SampleJobEvent(const SampleBuffer &from_samples,
											 const SampleBuffer &to_samples,
											 SampleBuffer &out_samples,
											 double time_in) const
{
	for (size_t i = 0; i < out_samples.sample_count(); i++) {
		double this_sample_time =
			out_samples.audio_params().samples_to_time(i).to_double() + time_in;
		double progress = get_total_progress(this_sample_time);

		for (int j = 0; j < out_samples.audio_params().channel_count(); j++) {
			out_samples.data(j)[i] = 0;

			if (from_samples.is_allocated()) {
				if (i < from_samples.sample_count()) {
					out_samples.data(j)[i] += from_samples.data(j)[i] *
											  transform_curve(1.0 - progress);
				}
			}

			if (to_samples.is_allocated()) {
				// Offset input samples from the end
				size_t remain =
					(out_samples.sample_count() - to_samples.sample_count());
				if (i >= remain) {
					int64_t in_index = i - remain;
					out_samples.data(j)[i] +=
						to_samples.data(j)[in_index] * transform_curve(progress);
				}
			}
		}
	}
}

}
