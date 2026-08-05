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

#ifndef OAK_TRANSITIONBLOCK_H
#define OAK_TRANSITIONBLOCK_H

#include "block/block.h"

namespace olive
{

class ClipBlock;

class TransitionBlock : public Block {
public:
	TransitionBlock();

	virtual void retranslate() override;

	Rational in_offset() const;
	Rational out_offset() const;

	/**
   * @brief Return the "middle point" of the transition, relative to the transition
   *
   * Used to calculate in/out offsets.
   *
   * 0 means the center of the transition is right in the middle and the in and out offsets will
   * be equal.
   */
	Rational offset_center() const;
	void set_offset_center(const Rational &r);

	void set_offsets_and_length(const Rational &in_offset,
								const Rational &out_offset);

	bool is_dual_transition() const
	{
		return connected_out_block() && connected_in_block();
	}

	Block *connected_out_block() const;
	Block *connected_in_block() const;

	double get_total_progress(const double &time) const;
	double get_out_progress(const double &time) const;
	double get_in_progress(const double &time) const;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void invalidate_cache(
		const TimeRange &range, const std::string &from, int element = -1,
		InvalidateCacheOptions options = InvalidateCacheOptions()) override;

	static const std::string k_out_block_input;
	static const std::string k_in_block_input;
	static const std::string k_curve_input;
	static const std::string k_center_input;

protected:
	virtual void ShaderJobEvent(const NodeValueRow &value, ShaderJob *job) const
	{
	}

	virtual void SampleJobEvent(const SampleBuffer &from_samples,
								const SampleBuffer &to_samples,
								SampleBuffer &out_samples, double time_in) const
	{
	}

	double transform_curve(double linear) const;

	virtual void InputConnectedEvent(const std::string &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const std::string &input, int element,
										Node *output) override;

	virtual TimeRange input_time_adjustment(const std::string &input, int element,
										  const TimeRange &input_time,
										  bool clamp) const override;

	virtual TimeRange
	output_time_adjustment(const std::string &input, int element,
						 const TimeRange &input_time) const override;

private:
	enum CurveType { k_linear, k_exponential, k_logarithmic };

	double get_internal_transition_time(const double &time) const;

	void insert_transition_times(AcceleratedJob *job, const double &time) const;

	ClipBlock *connected_out_block_;

	ClipBlock *connected_in_block_;
};

}

#endif // OAK_TRANSITIONBLOCK_H
