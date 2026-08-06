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

#ifndef OAK_OIIODECODER_H
#define OAK_OIIODECODER_H

#include <memory>
#include <string>
#include <vector>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>

#include "decoder.h"

namespace olive
{

class OIIODecoder : public Decoder {
public:
	OIIODecoder();

	DECODER_DEFAULT_DESTRUCTOR(OIIODecoder)

	virtual std::string id() const override;

	virtual bool supports_video() override
	{
		return true;
	}

	virtual FootageDescription probe(const std::string &filename,
									 OakCancelAtom *cancelled) const override;

protected:
	virtual bool open_internal() override;
	virtual OakRenderTexture *
	retrieve_video_internal(const RetrieveVideoParams &p) override;
	virtual FramePtr
	retrieve_video_frame_internal(const RetrieveVideoParams &p) override;
	virtual void close_internal() override;

private:
	std::unique_ptr<OIIO::ImageInput> image_;

	static bool file_type_is_supported(const std::string &fn);

	bool open_image_handler(const std::string &fn, int subimage);

	void close_image_handle();

	static OakVideoParams
	get_video_params_from_image_spec(const OIIO::ImageSpec &spec);

	PixelFormat pix_fmt_;
	OIIO::TypeDesc::BASETYPE oiio_pix_fmt_;

	Frame buffer_;
	RetrieveVideoParams last_params_;

	static std::vector<std::string> supported_formats;
};

}

#endif // OAK_OIIODECODER_H
