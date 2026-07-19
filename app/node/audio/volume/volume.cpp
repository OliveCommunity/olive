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

#include "volume.h"

#include "widget/slider/floatslider.h"

namespace olive
{

const QString VolumeNode::k_samples_input = QStringLiteral("samples_in");
const QString VolumeNode::k_volume_input = QStringLiteral("volume_in");

#define super MathNodeBase

VolumeNode::VolumeNode()
{
	add_input(k_samples_input, NodeValue::k_samples,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_volume_input, NodeValue::k_float, 1.0);
	set_input_property(k_volume_input, QStringLiteral("min"), 0.0);
	set_input_property(k_volume_input, QStringLiteral("view"),
					 FloatSlider::k_decibel);

	set_flag(k_audio_effect);
	set_effect_input(k_samples_input);
}

QString VolumeNode::name() const
{
	return tr("Volume");
}

QString VolumeNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.volume");
}

QVector<Node::CategoryID> VolumeNode::category() const
{
	return { k_category_filter };
}

QString VolumeNode::description() const
{
	return tr("Adjusts the volume of an audio source.");
}

void VolumeNode::value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const
{
	// Create a sample job
	SampleBuffer buffer = value[k_samples_input].to_samples();

	if (buffer.is_allocated()) {
		// If the input is static, we can just do it now which will be faster
		if (is_input_static(k_volume_input)) {
			auto volume = value[k_volume_input].to_double();

			if (!qFuzzyCompare(volume, 1.0)) {
				buffer.transform_volume(volume);
			}

			table->push(NodeValue::k_samples, QVariant::fromValue(buffer), this);
		} else {
			// Requires job
			SampleJob job(globals.time(), k_samples_input, value);
			job.insert(k_volume_input, value);
			table->push(NodeValue::k_samples, QVariant::fromValue(job), this);
		}
	}
}

void VolumeNode::process_samples(const NodeValueRow &values,
								const SampleBuffer &input, SampleBuffer &output,
								int index) const
{
	return process_samples_internal(values, k_op_multiply, k_samples_input,
								  k_volume_input, input, output, index);
}

void VolumeNode::retranslate()
{
	super::retranslate();

	set_input_name(k_samples_input, tr("Samples"));
	set_input_name(k_volume_input, tr("Volume"));
}

}
