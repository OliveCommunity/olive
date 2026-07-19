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

#ifndef OAK_SUBTITLEBLOCK_H
#define OAK_SUBTITLEBLOCK_H

#include "node/block/clip/clip.h"

namespace olive
{

class SubtitleBlock : public ClipBlock {
	Q_OBJECT
public:
	SubtitleBlock();

	NODE_DEFAULT_FUNCTIONS(SubtitleBlock)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	static const QString k_text_in;

	QString get_text() const
	{
		return get_standard_value(k_text_in).toString();
	}

	void set_text(const QString &text)
	{
		set_standard_value(k_text_in, text);
	}
};

}

#endif // OAK_SUBTITLEBLOCK_H
