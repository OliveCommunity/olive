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

#include "oiioencoder.h"

#include <OpenImageIO/imageio.h>

#include "common/oiioutils.h"

OIIO_NAMESPACE_USING

namespace olive
{

OIIOEncoder::OIIOEncoder(const EncodingParams &params) : Encoder(params) {}

bool OIIOEncoder::open()
{
	return true;
}

bool OIIOEncoder::write_frame(FramePtr frame, Rational time)
{
	std::string filename = get_filename_for_frame(time);

	auto output = OIIO::ImageOutput::create(filename);
	if (!output) {
		return false;
	}

	int base_type = 0; // OIIO::TypeDesc::UNKNOWN
	OakOIIOUtils oiio_utils = oakcommon_oiioutils_init();
	oakcommon_oiioutils_get_oiio_base_type_from_format(
		oiio_utils, frame->format(), &base_type);
	oakcommon_oiioutils_free(&oiio_utils);

	OIIO::TypeDesc type(static_cast<OIIO::TypeDesc::BASETYPE>(base_type));
	OIIO::ImageSpec spec(frame->width(), frame->height(),
						 frame->channel_count(), type);

	if (!output->open(filename, spec)) {
		return false;
	}

	if (!output->write_image(type, frame->data(), OIIO::AutoStride,
							 frame->linesize_bytes())) {
		return false;
	}

	if (!output->close()) {
		return false;
	}

	return true;
}

bool OIIOEncoder::write_audio(const SampleBuffer &audio)
{
	// Do nothing
	return false;
}

bool OIIOEncoder::write_subtitle(const char *text, double in_seconds,
								 double out_seconds)
{
	return false;
}

void OIIOEncoder::close()
{
	// Do nothing
}

}
