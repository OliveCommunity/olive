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

#include "colorprocessor.h"

#include "common/define.h"
#include "common/ocioutils.h"
#include "node/color/colormanager/colormanager.h"

namespace olive
{

ColorProcessor::ColorProcessor(ColorManager *config, const QString &input,
							   const ColorTransform &transform,
							   Direction direction)
{
	processor_ = nullptr;
	cpu_processor_ = nullptr;

	try {
		// Resolve role names (e.g. "scene_linear") to canonical colorspace names
		// so they can be passed to getProcessor()/DisplayViewTransform.
		QString resolved_input = input;
		ocio::ConstConfigRcPtr ocio_config = config->get_config();
		if (ocio_config && ocio_config->hasRole(input.toUtf8())) {
			resolved_input = ocio_config->getCanonicalName(input.toUtf8());
		}

		const QString &output = (transform.output().isEmpty()) ?
									config->get_default_display() :
									transform.output();

		if (transform.is_display()) {
			const QString &view = (transform.view().isEmpty()) ?
									  config->get_default_view(output) :
									  transform.view();

			auto display_transform = ocio::DisplayViewTransform::Create();

			display_transform->setSrc(resolved_input.toUtf8());
			display_transform->setDisplay(output.toUtf8());
			display_transform->setView(view.toUtf8());
			display_transform->setDirection(direction == k_normal ?
												ocio::TRANSFORM_DIR_FORWARD :
												ocio::TRANSFORM_DIR_INVERSE);

			if (transform.look().isEmpty()) {
				processor_ = ocio_config->getProcessor(display_transform);
			} else {
				auto group = ocio::GroupTransform::Create();

				const char *out_cs =
					ocio::LookTransform::GetLooksResultColorSpace(
						ocio_config, ocio_config->getCurrentContext(),
						transform.look().toUtf8());

				auto lt = ocio::LookTransform::Create();
				lt->setSrc(resolved_input.toUtf8());
				lt->setDst(out_cs);
				lt->setLooks(transform.look().toUtf8());
				lt->setSkipColorSpaceConversion(false);
				group->appendTransform(lt);

				display_transform->setSrc(out_cs);
				group->appendTransform(display_transform);

				processor_ = ocio_config->getProcessor(group);
			}

		} else {
			if (direction == k_normal) {
				processor_ = ocio_config->getProcessor(resolved_input.toUtf8(),
													   output.toUtf8());
			} else {
				processor_ = ocio_config->getProcessor(output.toUtf8(),
													   resolved_input.toUtf8());
			}
		}

		if (processor_) {
			cpu_processor_ = processor_->getDefaultCPUProcessor();
		}
	} catch (ocio::Exception &e) {
		qWarning() << "ColorProcessor exception:" << e.what();
	}
}

ColorProcessor::ColorProcessor(ocio::ConstProcessorRcPtr processor)
{
	processor_ = processor;
	cpu_processor_ = processor_ ? processor_->getDefaultCPUProcessor() :
								  nullptr;
}

void ColorProcessor::convert_frame(Frame *f)
{
	if (!cpu_processor_) {
		return;
	}

	ocio::BitDepth ocio_bit_depth =
		OCIOUtils::get_ocio_bit_depth_from_pixel_format(f->format());

	if (ocio_bit_depth == ocio::BIT_DEPTH_UNKNOWN) {
		qCritical() << "Tried to color convert frame with no format";
		return;
	}

	ocio::PackedImageDesc img(f->data(), f->width(), f->height(),
							  f->channel_count(), ocio_bit_depth,
							  ocio::AutoStride, ocio::AutoStride,
							  f->linesize_bytes());

	cpu_processor_->apply(img);
}

Color ColorProcessor::convert_color(const Color &in)
{
	if (!cpu_processor_) {
		return in;
	}

	// I've been bamboozled
	float c[4] = { float(in.red()), float(in.green()), float(in.blue()),
				   float(in.alpha()) };

	cpu_processor_->applyRGBA(c);

	return Color(c[0], c[1], c[2], c[3]);
}

ColorProcessorPtr ColorProcessor::create(ColorManager *config,
										 const QString &input,
										 const ColorTransform &transform,
										 Direction direction)
{
	return std::make_shared<ColorProcessor>(config, input, transform,
											direction);
}

ColorProcessorPtr ColorProcessor::create(ocio::ConstProcessorRcPtr processor)
{
	return std::make_shared<ColorProcessor>(processor);
}

ocio::ConstProcessorRcPtr ColorProcessor::get_processor()
{
	return processor_;
}

void ColorProcessor::convert_frame(FramePtr f)
{
	convert_frame(f.get());
}

}
