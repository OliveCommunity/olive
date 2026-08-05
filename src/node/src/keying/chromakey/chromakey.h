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

#include "color/ociobase/ociobase.h"

namespace olive
{

class ChromaKeyNode : public OCIOBaseNode {
public:
	ChromaKeyNode();

	NODE_DEFAULT_FUNCTIONS(ChromaKeyNode)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;

	virtual void InputValueChangedEvent(const std::string &input,
										int element) override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void config_changed() override;

	// Maps the misspelled tolerance input IDs from old project files onto the
	// corrected ones
	virtual std::string
	get_input_id_for_legacy_id(const std::string &id) const override;

	static const std::string k_color_input;
	static const std::string k_invert_input;
	static const std::string k_mask_only_input;
	static const std::string k_upper_tolerance_input;
	static const std::string k_lower_tolerance_input;
	static const std::string k_garbage_matte_input;
	static const std::string k_core_matte_input;
	static const std::string k_shadows_input;
	static const std::string k_highlights_input;

private:
	void generate_processor();
};

} // namespace olive

#endif // OAK_CHROMAKEYNODE_H
