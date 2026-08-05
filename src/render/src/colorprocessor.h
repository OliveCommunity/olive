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

#ifndef OAK_COLORPROCESSOR_H
#define OAK_COLORPROCESSOR_H

#include <memory>
#include <string>
#include <vector>

#include "codec/frame.h"
#include "colortransform.h"
#include "define.h"
#include "ocioutils.h"

namespace olive
{

class ColorManager;

class ColorProcessor;
using ColorProcessorPtr = std::shared_ptr<ColorProcessor>;

class ColorProcessor {
public:
	enum Direction { k_normal, k_inverse };

	ColorProcessor(ColorManager *config, const std::string &input,
				   const ColorTransform &dest_space,
				   Direction direction = k_normal);
	ColorProcessor(ocio::ConstProcessorRcPtr processor);

	DISABLE_COPY_MOVE(ColorProcessor)

	static ColorProcessorPtr create(ColorManager *config,
									const std::string &input,
									const ColorTransform &dest_space,
									Direction direction = k_normal);
	static ColorProcessorPtr create(ocio::ConstProcessorRcPtr processor);

	ocio::ConstProcessorRcPtr get_processor();

	void convert_frame(FramePtr f);
	void convert_frame(Frame *f);

	Color convert_color(const Color &in);

	const char *id() const
	{
		return processor_->getCacheID();
	}

private:
	ocio::ConstProcessorRcPtr processor_;

	ocio::ConstCPUProcessorRcPtr cpu_processor_;
};

using ColorProcessorChain = std::vector<ColorProcessorPtr>;

}

#endif // OAK_COLORPROCESSOR_H
