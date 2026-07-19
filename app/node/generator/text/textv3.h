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

#ifndef OAK_TEXTGENERATORV3_H
#define OAK_TEXTGENERATORV3_H

#include "node/generator/shape/shapenodebase.h"
#include "node/gizmo/text.h"

namespace olive
{

class TextGeneratorV3 : public ShapeNodeBase {
	Q_OBJECT
public:
	TextGeneratorV3();

	NODE_DEFAULT_FUNCTIONS(TextGeneratorV3)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void generate_frame(FramePtr frame,
							   const GenerateJob &job) const override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	enum VerticalAlignment { k_v_align_top, k_v_align_middle, k_v_align_bottom };

	VerticalAlignment get_vertical_alignment() const
	{
		return static_cast<VerticalAlignment>(
			get_standard_value(k_vertical_alignment_input).toInt());
	}

	static Qt::Alignment get_qt_alignment_from_ours(VerticalAlignment v);
	static VerticalAlignment get_our_alignment_from_qts(Qt::Alignment v);

	static const QString k_text_input;
	static const QString k_vertical_alignment_input;
	static const QString k_use_args_input;
	static const QString k_args_input;

	static QString format_string(const QString &input, const QStringList &args);

protected:
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

private:
	TextGizmo *text_gizmo_;

	bool dont_emit_valign_;

private slots:
	void gizmo_activated();
	void gizmo_deactivated();
	void set_vertical_alignment_undoable(Qt::Alignment a);
};

}

#endif // OAK_TEXTGENERATORV3_H
