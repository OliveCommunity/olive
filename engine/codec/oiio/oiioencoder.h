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

#ifndef OAK_OIIOENCODER_H
#define OAK_OIIOENCODER_H

#include "codec/encoder.h"

namespace olive
{

class OIIOEncoder : public Encoder {
	Q_OBJECT
public:
	OIIOEncoder(const EncodingParams &params);

public slots:
	virtual bool open() override;

	virtual bool write_frame(olive::FramePtr frame,
							olive::core::Rational time) override;
	virtual bool write_audio(const SampleBuffer &audio) override;
	virtual bool write_subtitle(const SubtitleBlock *sub_block) override;

	virtual void close() override;
};

}

#endif // OAK_OIIOENCODER_H
