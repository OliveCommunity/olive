/***
  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team
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

#ifndef OAK_CHROMAKEYNODE_H
#define OAK_CHROMAKEYNODE_H

#include "node/color/ociobase/ociobase.h"

namespace olive
{

class ChromaKeyNode : public OCIOBaseNode {
	Q_OBJECT
public:
	ChromaKeyNode();

	NODE_DEFAULT_FUNCTIONS(ChromaKeyNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void config_changed() override;

	// Maps the misspelled tolerance input IDs from old project files onto the
	// corrected ones
	virtual QString get_input_id_for_legacy_id(const QString &id) const override;

	static const QString k_color_input;
	static const QString k_invert_input;
	static const QString k_mask_only_input;
	static const QString k_upper_tolerance_input;
	static const QString k_lower_tolerance_input;
	static const QString k_garbage_matte_input;
	static const QString k_core_matte_input;
	static const QString k_shadows_input;
	static const QString k_highlights_input;

private:
	void generate_processor();
};

} // namespace olive

#endif // OAK_CHROMAKEYNODE_H
