/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef OAK_OCIOGRADINGTRANSFORMLOGNODE_H
#define OAK_OCIOGRADINGTRANSFORMLOGNODE_H

#include "node/color/ociobase/ociobase.h"
#include "render/colorprocessor.h"

namespace olive
{

/**
 * @brief Lift/gamma/gain grading node built on ocio::GRADING_LOG
 *
 * Mirrors OCIOGradingTransformLinearNode for the log grading style. OCIO's
 * log-style GPU uniforms map to the classic wheels as: brightness = lift,
 * contrast = gain, gamma = gamma.
 */
class OCIOGradingTransformLogNode : public OCIOBaseNode {
	Q_OBJECT
public:
	OCIOGradingTransformLogNode();

	NODE_DEFAULT_FUNCTIONS(OCIOGradingTransformLogNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;
	virtual void InputConnectedEvent(const QString &input, int element,
									 Node *output) override;
	virtual void InputDisconnectedEvent(const QString &input, int element,
										Node *output) override;
	void generate_processor();

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const QString k_lift_input;
	static const QString k_gain_input;
	static const QString k_gamma_input;
	static const QString k_saturation_input;
	static const QString k_pivot_input;
	static const QString k_clamp_black_enable_input;
	static const QString k_clamp_black_input;
	static const QString k_clamp_white_enable_input;
	static const QString k_clamp_white_input;

protected slots:
	virtual void config_changed() override;

private:
	void set_vec4_input_colors(const QString &input);

	/**
	 * @brief Constrains the white clamp UI minimum to just above the black
	 * clamp, as required by ocio::GradingPrimary::validate
	 *
	 * Only applies while the black clamp is a static value; when it is
	 * keyframed or connected the invariant is enforced per frame in Value()
	 * instead.
	 */
	void update_clamp_white_minimum();
};

} // olive

#endif
