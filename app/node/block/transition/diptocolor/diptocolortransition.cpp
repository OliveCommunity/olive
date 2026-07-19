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

#include "diptocolortransition.h"

namespace olive
{

const QString DipToColorTransition::k_color_input = QStringLiteral("color_in");

#define super TransitionBlock

DipToColorTransition::DipToColorTransition()
{
	add_input(k_color_input, NodeValue::k_color,
			 QVariant::fromValue(Color(0, 0, 0)));
}

QString DipToColorTransition::name() const
{
	return tr("Dip To Color");
}

QString DipToColorTransition::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.diptocolor");
}

QVector<Node::CategoryID> DipToColorTransition::category() const
{
	return { k_category_transition };
}

QString DipToColorTransition::description() const
{
	return tr("Transition between clips by dipping to a color.");
}

ShaderCode
DipToColorTransition::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)

	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/diptoblack.frag"),
		QString());
}

void DipToColorTransition::retranslate()
{
	super::retranslate();

	set_input_name(k_color_input, tr("Color"));
}

void DipToColorTransition::ShaderJobEvent(const NodeValueRow &value,
										  ShaderJob *job) const
{
	job->insert(k_color_input, value);
}

}
