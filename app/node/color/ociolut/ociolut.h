/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_OCIOLUTNODE_H
#define OAK_OCIOLUTNODE_H

#include <QMutex>

#include "node/color/ociobase/ociobase.h"
#include "render/colorprocessor.h"

namespace olive
{

class OCIOLutNode : public OCIOBaseNode {
	Q_OBJECT
public:
	OCIOLutNode();

	NODE_DEFAULT_FUNCTIONS(OCIOLutNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const QString k_file_input;
	static const QString k_direction_input;

	/**
	 * @brief Human-readable description of why no LUT processor is active
	 *
	 * Empty when a valid LUT processor is in use or no LUT file has been
	 * selected yet. This allows the UI (and tests) to surface silent
	 * passthrough states (missing file, unsupported extension, OCIO errors).
	 */
	const QString &last_error() const
	{
		return last_error_;
	}

protected slots:
	virtual void config_changed() override;

private:
	void generate_processor();
	void ensure_processor() const;
	bool create_processor_from_inputs() const;

	void set_last_error(const QString &error) const;

	mutable QMutex gen_mutex_;
	mutable bool processor_dirty_ = true;
	mutable QString last_path_;
	mutable int last_direction_ = -1;
	mutable ColorProcessorPtr last_processor_;
	mutable QString last_error_;
};

} // namespace olive

#endif // OAK_OCIOLUTNODE_H
