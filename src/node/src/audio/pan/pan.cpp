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

#include "pan.h"

#include <cmath>

#include "sliderdisplaytype.h"

namespace olive
{

const std::string PanNode::k_samples_input = "samples_in";
const std::string PanNode::k_panning_input = "panning_in";

#define super Node

PanNode::PanNode()
{
	add_input(k_samples_input, NodeValue::k_samples,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_panning_input, NodeValue::k_float, 0.0);
	set_input_property(k_panning_input, "min", -1.0);
	set_input_property(k_panning_input, "max", 1.0);
	set_input_property(k_panning_input, "view",
					 slider::k_percentage);

	set_flag(k_audio_effect);
	set_effect_input(k_samples_input);
}

std::string PanNode::name() const
{
	return "Pan";
}

std::string PanNode::id() const
{
	return "org.olivevideoeditor.Olive.pan";
}

std::vector<Node::CategoryID> PanNode::category() const
{
	return { k_category_filter };
}

std::string PanNode::description() const
{
	return "Adjust the stereo panning of an audio source.";
}

void PanNode::value(const NodeValueRow &value, const NodeGlobals &globals,
					NodeValueTable *table) const
{
	// Create a sample job
	SampleBuffer samples = value.at(k_samples_input).to_samples();
	if (samples.is_allocated()) {
		// This node is only compatible with stereo audio
		if (samples.audio_params().channel_count() == 2) {
			// If the input is static, we can just do it now which will be faster
			if (is_input_static(k_panning_input)) {
				float pan_volume = value.at(k_panning_input).to_double();
				if (pan_volume != 0.0f) {
					if (pan_volume > 0) {
						samples.transform_volume_for_channel(0,
															 1.0f - pan_volume);
					} else {
						samples.transform_volume_for_channel(1,
															 1.0f + pan_volume);
					}
				}

				table->push(NodeValue(NodeValue::k_samples, samples, this));
			} else {
				// Requires job
				SampleJob job(globals.time(), k_samples_input, value);
				job.insert(k_panning_input, value);
				table->push(NodeValue::k_samples, Variant::from_value(job),
							this);
			}
		} else {
			// Pass right through
			table->push(value.at(k_samples_input));
		}
	}
}

void PanNode::process_samples(const NodeValueRow &values,
							 const SampleBuffer &input, SampleBuffer &output,
							 int index) const
{
	float pan_val = values.at(k_panning_input).to_double();

	for (int i = 0; i < input.audio_params().channel_count(); i++) {
		output.data(i)[index] = input.data(i)[index];
	}

	if (pan_val > 0) {
		output.data(0)[index] *= (1.0F - pan_val);
	} else if (pan_val < 0) {
		output.data(1)[index] *= (1.0F - std::abs(pan_val));
	}
}

void PanNode::retranslate()
{
	super::retranslate();

	set_input_name(k_samples_input, "Samples");
	set_input_name(k_panning_input, "Pan");
}

}
