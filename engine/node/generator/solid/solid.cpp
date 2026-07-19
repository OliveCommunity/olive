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

#include "solid.h"

namespace olive
{

const QString SolidGenerator::k_color_input = QStringLiteral("color_in");

#define super Node

SolidGenerator::SolidGenerator()
{
	// Default to a color that isn't black
	add_input(k_color_input, NodeValue::k_color,
			 QVariant::fromValue(Color(1.0f, 0.0f, 0.0f, 1.0f)));
}

QString SolidGenerator::name() const
{
	return tr("Solid");
}

QString SolidGenerator::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.solidgenerator");
}

QVector<Node::CategoryID> SolidGenerator::category() const
{
	return { k_category_generator };
}

QString SolidGenerator::description() const
{
	return tr("Generate a solid color.");
}

void SolidGenerator::retranslate()
{
	super::retranslate();

	set_input_name(k_color_input, tr("Color"));
}

void SolidGenerator::value(const NodeValueRow &value,
						   const NodeGlobals &globals,
						   NodeValueTable *table) const
{
	table->push(NodeValue::k_texture,
				Texture::job(globals.vparams(), ShaderJob(value)), this);
}

ShaderCode SolidGenerator::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)

	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/solid.frag"));
}

}
