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

#ifndef OAK_DISPLAYTRANSFORMNODE_H
#define OAK_DISPLAYTRANSFORMNODE_H

#include "node/color/ociobase/ociobase.h"
#include "render/colorprocessor.h"

namespace olive
{

class DisplayTransformNode : public OCIOBaseNode {
	Q_OBJECT
public:
	DisplayTransformNode();

	NODE_DEFAULT_FUNCTIONS(DisplayTransformNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

	QString get_display() const;
	QString get_view() const;
	ColorProcessor::Direction get_direction() const;

	static const QString k_display_input;
	static const QString k_view_input;
	static const QString k_direction_input;

protected slots:
	virtual void config_changed() override;

private:
	void generate_processor();

	void update_displays();

	void update_views();
};

} // olive

#endif // OAK_DISPLAYTRANSFORMNODE_H
