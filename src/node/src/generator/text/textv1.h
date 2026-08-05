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

#ifndef OAK_TEXTGENERATORV1_H
#define OAK_TEXTGENERATORV1_H

#include "node.h"

namespace olive
{

class TextGeneratorV1 : public Node {
public:
	TextGeneratorV1();

	NODE_DEFAULT_FUNCTIONS(TextGeneratorV1)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void generate_frame(FramePtr frame,
							   const GenerateJob &job) const override;

	static const std::string k_text_input;
	static const std::string k_html_input;
	static const std::string k_color_input;
	static const std::string k_v_align_input;
	static const std::string k_font_input;
	static const std::string k_font_size_input;
};

}

#endif // OAK_TEXTGENERATORV1_H
