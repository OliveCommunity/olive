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

#include "transition.h"

#include "block/clip/clip.h"
#include "output/track/track.h"
#include "sliderdisplaytype.h"

namespace olive
{

#define super Block

const std::string TransitionBlock::k_out_block_input = "out_block_in";
const std::string TransitionBlock::k_in_block_input = "in_block_in";
const std::string TransitionBlock::k_curve_input = "curve_in";
const std::string TransitionBlock::k_center_input = "center_in";

TransitionBlock::TransitionBlock()
	: connected_out_block_(nullptr)
	, connected_in_block_(nullptr)
{
	add_input(k_out_block_input, NodeValue::k_none,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_in_block_input, NodeValue::k_none,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_curve_input, NodeValue::k_combo,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));

	add_input(k_center_input, NodeValue::k_rational,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));
	set_input_property(k_center_input, "view", slider::k_time);
	set_input_property(k_center_input, "viewlock", true);

	set_flag(k_dont_show_in_param_view, false);
}

void TransitionBlock::retranslate()
{
	super::retranslate();

	set_input_name(k_out_block_input, "From");
	set_input_name(k_in_block_input, "To");
	set_input_name(k_curve_input, "Curve");
	set_input_name(k_center_input, "Center Offset");

	// These must correspond to the CurveType enum
	set_combo_box_strings(k_curve_input,
					   { "Linear", "Exponential", "Logarithmic" });
}

Rational TransitionBlock::in_offset() const
{
	if (is_dual_transition()) {
		return length() / 2 + offset_center();
	} else if (connected_in_block()) {
		return length();
	} else {
		return 0;
	}
}

Rational TransitionBlock::out_offset() const
{
	if (is_dual_transition()) {
		return length() / 2 - offset_center();
	} else if (connected_out_block()) {
		return length();
	} else {
		return 0;
	}
}

Rational TransitionBlock::offset_center() const
{
	return get_standard_value(k_center_input).value<Rational>();
}

void TransitionBlock::set_offset_center(const Rational &r)
{
	set_standard_value(k_center_input, Variant::from_value(r));
}

void TransitionBlock::set_offsets_and_length(const Rational &in_offset,
											 const Rational &out_offset)
{
	Rational len = in_offset + out_offset;
	Rational center = len / 2 - in_offset;

	set_length_and_media_out(len);
	set_offset_center(center);
}

Block *TransitionBlock::connected_out_block() const
{
	return connected_out_block_;
}

Block *TransitionBlock::connected_in_block() const
{
	return connected_in_block_;
}

double TransitionBlock::get_total_progress(const double &time) const
{
	return get_internal_transition_time(time) / length().to_double();
}

double TransitionBlock::get_out_progress(const double &time) const
{
	if (out_offset() == 0) {
		return 0;
	}

	return std::clamp(
		1.0 - (get_internal_transition_time(time) / out_offset().to_double()), 0.0,
		1.0);
}

double TransitionBlock::get_in_progress(const double &time) const
{
	if (in_offset() == 0) {
		return 0;
	}

	return std::clamp(
		(get_internal_transition_time(time) - out_offset().to_double()) /
			in_offset().to_double(),
		0.0, 1.0);
}

double TransitionBlock::get_internal_transition_time(const double &time) const
{
	return time;
}

void TransitionBlock::insert_transition_times(AcceleratedJob *job,
											const double &time) const
{
	// Provides total transition progress from 0.0 (start) - 1.0 (end)
	job->insert("ove_tprog_all",
				NodeValue(NodeValue::k_float, get_total_progress(time), this));

	// Provides progress of out section from 1.0 (start) - 0.0 (end)
	job->insert("ove_tprog_out",
				NodeValue(NodeValue::k_float, get_out_progress(time), this));

	// Provides progress of in section from 0.0 (start) - 1.0 (end)
	job->insert("ove_tprog_in",
				NodeValue(NodeValue::k_float, get_in_progress(time), this));
}

void TransitionBlock::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	NodeValue out_buffer = value.at(k_out_block_input);
	NodeValue in_buffer = value.at(k_in_block_input);
	NodeValue::Type data_type = (out_buffer.type() != NodeValue::k_none) ?
									out_buffer.type() :
									in_buffer.type();

	NodeValue::Type job_type = NodeValue::k_none;
	Variant push_job;

	if (data_type == NodeValue::k_texture) {
		// This must be a visual transition
		ShaderJob job;

		if (out_buffer.type() != NodeValue::k_none) {
			job.insert(k_out_block_input, out_buffer);
		} else {
			job.insert(k_out_block_input, NodeValue(NodeValue::k_texture, nullptr));
		}

		if (in_buffer.type() != NodeValue::k_none) {
			job.insert(k_in_block_input, in_buffer);
		} else {
			job.insert(k_in_block_input, NodeValue(NodeValue::k_texture, nullptr));
		}

		job.insert(k_curve_input, value);

		double time = globals.time().in().to_double();
		insert_transition_times(&job, time);

		ShaderJobEvent(value, &job);

		job_type = NodeValue::k_texture;
		push_job = Variant::from_value(Texture::job(globals.vparams(), job));
	} else if (data_type == NodeValue::k_samples) {
		// This must be an audio transition
		SampleBuffer from_samples = out_buffer.to_samples();
		SampleBuffer to_samples = in_buffer.to_samples();

		if (from_samples.is_allocated() || to_samples.is_allocated()) {
			double time_in = globals.time().in().to_double();
			double time_out = globals.time().out().to_double();

			const AudioParams &params = (from_samples.is_allocated()) ?
											from_samples.audio_params() :
											to_samples.audio_params();

			SampleBuffer out_samples;

			if (params.is_valid()) {
				int nb_samples = params.time_to_samples(time_out - time_in);

				out_samples = SampleBuffer(params, nb_samples);
				SampleJobEvent(from_samples, to_samples, out_samples, time_in);
			}

			job_type = NodeValue::k_samples;
			push_job = Variant::from_value(out_samples);
		}
	}

	if (!push_job.is_null()) {
		table->push(job_type, push_job, this);
	}
}

void TransitionBlock::invalidate_cache(const TimeRange &range,
									  const std::string &from, int element,
									  InvalidateCacheOptions options)
{
	TimeRange r = range;

	if (from == k_out_block_input || from == k_in_block_input) {
		Block *n = dynamic_cast<Block *>(get_connected_output(from));
		if (n) {
			r = Track::transform_range_from_block(n, r);
		}
	}

	super::invalidate_cache(r, from, element, options);
}

double TransitionBlock::transform_curve(double linear) const
{
	switch (static_cast<CurveType>(get_standard_value(k_curve_input).to_int())) {
	case k_linear:
		break;
	case k_exponential:
		linear *= linear;
		break;
	case k_logarithmic:
		linear = std::sqrt(linear);
		break;
	}

	return linear;
}

void TransitionBlock::InputConnectedEvent(const std::string &input, int element,
										  Node *output)
{
	(void) element;

	if (input == k_out_block_input) {
		// If node is not a block, this will just be null
		if ((connected_out_block_ = dynamic_cast<ClipBlock *>(output))) {
			connected_out_block_->set_out_transition(this);
		}
	} else if (input == k_in_block_input) {
		// If node is not a block, this will just be null
		if ((connected_in_block_ = dynamic_cast<ClipBlock *>(output))) {
			connected_in_block_->set_in_transition(this);
		}
	}
}

void TransitionBlock::InputDisconnectedEvent(const std::string &input, int element,
											 Node *output)
{
	(void) element;
	(void) output;

	if (input == k_out_block_input) {
		if (connected_out_block_) {
			connected_out_block_->set_out_transition(nullptr);
			connected_out_block_ = nullptr;
		}
	} else if (input == k_in_block_input) {
		if (connected_in_block_) {
			connected_in_block_->set_in_transition(nullptr);
			connected_in_block_ = nullptr;
		}
	}
}

TimeRange TransitionBlock::input_time_adjustment(const std::string &input,
											   int element,
											   const TimeRange &input_time,
											   bool clamp) const
{
	if (input == k_in_block_input || input == k_out_block_input) {
		Block *block = dynamic_cast<Block *>(get_connected_output(input));
		if (block) {
			// Retransform time as if it came from the track
			return input_time + in() - block->in();
		}
	}

	return super::input_time_adjustment(input, element, input_time, clamp);
}

TimeRange
TransitionBlock::output_time_adjustment(const std::string &input, int element,
									  const TimeRange &input_time) const
{
	if (input == k_in_block_input || input == k_out_block_input) {
		Block *block = dynamic_cast<Block *>(get_connected_output(input));
		if (block) {
			return input_time + block->in() - in();
		}
	}

	return super::output_time_adjustment(input, element, input_time);
}

}
