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

#ifndef OAK_MATRIXGENERATOR_H
#define OAK_MATRIXGENERATOR_H

#include <QVector2D>

#include "node/node.h"
#include "node/inputdragger.h"

namespace olive
{

class MatrixGenerator : public Node {
	Q_OBJECT
public:
	MatrixGenerator();

	NODE_DEFAULT_FUNCTIONS(MatrixGenerator)

	virtual QString name() const override;
	virtual QString short_name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const QString k_position_input;
	static const QString k_rotation_input;
	static const QString k_scale_input;
	static const QString k_uniform_scale_input;
	static const QString k_anchor_input;

protected:
	QMatrix4x4 generate_matrix(const NodeValueRow &value, bool ignore_anchor,
							  bool ignore_position, bool ignore_scale,
							  const QMatrix4x4 &mat) const;
	static QMatrix4x4 generate_matrix(const QVector2D &pos, const float &rot,
									 const QVector2D &scale, bool uniform_scale,
									 const QVector2D &anchor, QMatrix4x4 mat);

	virtual void InputValueChangedEvent(const QString &input,
										int element) override;
};

}

#endif // TRANSFORMDISTORT_H
