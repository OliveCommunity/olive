/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "../../../include/render/color.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "alivecount.h"
#include "internalhandles.h"

#include "color/colormanager/colormanager.h"
#include "filefunctions.h"
#include <OpenColorIO/OpenColorIO.h>

namespace
{

int write_string(const std::string &s, char *buf, int n)
{
	const int required = int(s.size()) + 1;
	if (buf && n >= required) {
		std::memcpy(buf, s.c_str(), size_t(required));
	}
	return required;
}

} // namespace

OakColorProcessor oakrender_color_processor_create(const char *src_space,
												   const char *dst_transform,
												   int direction)
{
	if (!src_space || !*src_space || !dst_transform || !*dst_transform) {
		return OakColorProcessor{};
	}
	if (direction != OAKRENDER_COLOR_DIRECTION_NORMAL &&
		direction != OAKRENDER_COLOR_DIRECTION_INVERSE) {
		return OakColorProcessor{};
	}
	try {
		OCIO_NAMESPACE::ConstConfigRcPtr config = olive::ColorManager::get_default_config();
		if (!config) {
			return OakColorProcessor{};
		}

		// Resolve role names (e.g. "scene_linear") to canonical colorspace
		// names, mirroring ColorProcessor's constructor.
		std::string src = src_space;
		if (config->hasRole(src_space)) {
			src = config->getCanonicalName(src_space);
		}

		// OCIO_NAMESPACE failures are non-fatal (matching the C++ behavior): the
		// handle is still returned, but holds a null processor and
		// conversions pass through.
		OCIO_NAMESPACE::ConstProcessorRcPtr processor;
		try {
			if (direction == OAKRENDER_COLOR_DIRECTION_NORMAL) {
				processor = config->getProcessor(src.c_str(), dst_transform);
			} else {
				processor = config->getProcessor(dst_transform, src.c_str());
			}
		} catch (OCIO_NAMESPACE::Exception &) {
			processor = nullptr;
		}

		auto *impl = new OakColorProcessorImpl;
		impl->ptr = olive::ColorProcessor::create(processor);
		return oakrender_c_api::make_handle<OakColorProcessor>(
			impl, true, &oakrender_c_api::delete_as<OakColorProcessorImpl>);
	} catch (...) {
		return OakColorProcessor{};
	}
}

void oakrender_color_processor_free(OakColorProcessor *processor)
{
	oakrender_c_api::free_handle(processor);
}

int oakrender_color_processor_is_valid(OakColorProcessor processor)
{
	OakColorProcessorImpl *p =
		oakrender_c_api::to_native<OakColorProcessorImpl>(processor);
	return p && p->ptr && p->ptr->get_processor() ? 1 : 0;
}

int oakrender_color_processor_convert(OakColorProcessor processor,
									  double ir, double ig, double ib,
									  double ia, double *out_r, double *out_g,
									  double *out_b, double *out_a)
{
	OakColorProcessorImpl *p =
		oakrender_c_api::to_native<OakColorProcessorImpl>(processor);
	if (!p || !p->ptr || !out_r || !out_g || !out_b || !out_a) {
		return OAKRENDER_E_INVALID;
	}
	try {
		olive::Color out =
			p->ptr->convert_color(olive::Color(ir, ig, ib, ia));
		*out_r = out.red();
		*out_g = out.green();
		*out_b = out.blue();
		*out_a = out.alpha();
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

/* ---- ColorManager statics ------------------------------------------------- */

int oakrender_color_manager_set_up_default_config(void)
{
	try {
		olive::ColorManager::set_up_default_config();
		return olive::ColorManager::get_default_config() ? OAKRENDER_OK :
														   OAKRENDER_E_FAILED;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_color_manager_get_config(char *buf, int n)
{
	try {
		const char *ocio_env = std::getenv("OCIO");
		if (ocio_env && *ocio_env) {
			return write_string(ocio_env, buf, n);
		}
		if (!olive::ColorManager::get_default_config()) {
			return OAKRENDER_E_STATE;
		}
		// The default config is extracted next to the configuration
		// location (ColorManager::set_up_default_config()).
		return write_string(FileFunctions::get_configuration_location() +
								"/ocioconf/config.ocio",
							buf, n);
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_color_manager_display_transform(const char *display,
											  const char *view, char *buf,
											  int n)
{
	if (!display || !*display || !view || !*view) {
		return OAKRENDER_E_INVALID;
	}
	try {
		OCIO_NAMESPACE::ConstConfigRcPtr config = olive::ColorManager::get_default_config();
		if (!config) {
			return OAKRENDER_E_STATE;
		}

		bool display_found = false;
		for (int i = 0; i < config->getNumDisplays(); i++) {
			if (display == std::string(config->getDisplay(i))) {
				display_found = true;
				break;
			}
		}
		if (!display_found) {
			return OAKRENDER_E_NOT_FOUND;
		}

		bool view_found = false;
		for (int i = 0; i < config->getNumViews(display); i++) {
			if (view == std::string(config->getView(display, i))) {
				view_found = true;
				break;
			}
		}
		if (!view_found) {
			return OAKRENDER_E_NOT_FOUND;
		}

		// Source = the config's reference colorspace (role lookup).
		OCIO_NAMESPACE::ConstColorSpaceRcPtr ref_cs =
			config->getColorSpace(OCIO_NAMESPACE::ROLE_REFERENCE);
		if (!ref_cs) {
			return OAKRENDER_E_STATE;
		}

		auto dvt = OCIO_NAMESPACE::DisplayViewTransform::Create();
		dvt->setSrc(ref_cs->getName());
		dvt->setDisplay(display);
		dvt->setView(view);

		OCIO_NAMESPACE::ConstProcessorRcPtr processor = config->getProcessor(dvt);
		if (!processor) {
			return OAKRENDER_E_NOT_FOUND;
		}
		return write_string(processor->getCacheID(), buf, n);
	} catch (OCIO_NAMESPACE::Exception &) {
		return OAKRENDER_E_NOT_FOUND;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

int oakrender_color_processor_convert_frame(OakColorProcessor processor,
											OakCodecFrame frame)
{
	OakColorProcessorImpl *p =
		oakrender_c_api::to_native<OakColorProcessorImpl>(processor);
	OakCodecFrameImpl *f =
		oakrender_c_api::to_native<OakCodecFrameImpl>(frame);
	if (!p || !p->ptr || !f || !f->ptr) {
		return OAKRENDER_E_INVALID;
	}
	try {
		// In-place: ColorProcessor::convert_frame() applies the CPU
		// processor to the frame's pixel buffer through an
		// OCIO::PackedImageDesc view. A processor whose underlying OCIO
		// processor is null (creation failure was non-fatal) is a
		// pass-through and still reports success, mirroring the C++ API.
		p->ptr->convert_frame(f->ptr);
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}
