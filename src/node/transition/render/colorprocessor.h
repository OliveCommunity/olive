#pragma once
#include <memory>
#include <string>
#include "ocioutils.h"
#include "colortransform.h"
namespace olive {
class ColorManager;
class ColorProcessor;
using ColorProcessorPtr = std::shared_ptr<ColorProcessor>;
class ColorProcessor {
public:
	enum Direction { k_normal, k_inverse };
	static ColorProcessorPtr create(ColorManager *, const std::string &,
									const ColorTransform &,
									Direction = k_normal)
	{
		return nullptr;
	}
	static ColorProcessorPtr create(ocio::ConstProcessorRcPtr)
	{
		return nullptr;
	}
};
}
