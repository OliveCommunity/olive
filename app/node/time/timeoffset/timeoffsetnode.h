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

#ifndef OAK_TIMEOFFSETNODE_H
#define OAK_TIMEOFFSETNODE_H

#include "node/node.h"

namespace olive
{

class TimeOffsetNode : public Node {
public:
	TimeOffsetNode();

	NODE_DEFAULT_FUNCTIONS(TimeOffsetNode)

	virtual QString name() const override
	{
		return tr("Time Offset");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.Olive.timeoffset");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_time };
	}

	virtual QString description() const override
	{
		return tr("Offset time passing through the graph.");
	}

	virtual TimeRange input_time_adjustment(const QString &input, int element,
										  const TimeRange &input_time,
										  bool clamp) const override;
	virtual TimeRange
	output_time_adjustment(const QString &input, int element,
						 const TimeRange &input_time) const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const QString k_time_input;
	static const QString k_input_input;

private:
	Rational get_remapped_time(const Rational &input) const;
	Rational get_remapped_output_time(const Rational &input) const;
};

}

#endif // OAK_TIMEOFFSETNODE_H
