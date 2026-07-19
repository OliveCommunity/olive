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

#ifndef OAK_MULTICAMDISPLAY_H
#define OAK_MULTICAMDISPLAY_H

#include "node/input/multicam/multicamnode.h"
#include "widget/viewer/viewerdisplay.h"

namespace olive
{

class MulticamDisplay : public ViewerDisplayWidget {
	Q_OBJECT
public:
	explicit MulticamDisplay(QWidget *parent = nullptr);

	void set_multicam_node(MultiCamNode *n);

protected:
	virtual void on_paint() override;

	virtual void on_destroy() override;

	virtual TexturePtr load_custom_texture_from_frame(const QVariant &v) override;

private:
	static QString generate_shader_code(int rows, int cols);

	MultiCamNode *node_;

	QVariant shader_;
	int rows_;
	int cols_;
};

}

#endif // OAK_MULTICAMDISPLAY_H
