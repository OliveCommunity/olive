/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_MULTICAMNODE_H
#define OAK_MULTICAMNODE_H

#include "node/node.h"
#include "node/output/track/tracklist.h"

namespace olive
{

class Sequence;

class MultiCamNode : public Node {
	Q_OBJECT
public:
	MultiCamNode();

	NODE_DEFAULT_FUNCTIONS(MultiCamNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual ActiveElements
	get_active_elements_at_time(const QString &input,
							const TimeRange &r) const override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void retranslate() override;

	static const QString k_current_input;
	static const QString k_sources_input;
	static const QString k_sequence_input;
	static const QString k_sequence_type_input;

	int get_current_source() const
	{
		return get_standard_value(k_current_input).toInt();
	}

	int get_source_count() const;

	static void get_rows_and_columns(int sources, int *rows, int *cols);
	void get_rows_and_columns(int *rows, int *cols) const
	{
		return get_rows_and_columns(get_source_count(), rows, cols);
	}

	void set_sequence_type(Track::Type t)
	{
		set_standard_value(k_sequence_type_input, t);
	}

	static void index_to_row_cols(int index, int total_rows, int total_cols,
							   int *row, int *col);

	static int rows_cols_to_index(int row, int col, int total_rows, int total_cols)
	{
		return col + row * total_cols;
	}

	virtual Node *get_connected_render_output(const QString &input,
										   int element = -1) const override;
	virtual bool is_input_connected_for_render(const QString &input,
										   int element = -1) const override;

	virtual QVector<QString> ignore_inputs_for_rendering() const override;

protected:
	virtual void InputConnectedEvent(const QString &input, int element,
									 Node *output) override;
	virtual void InputDisconnectedEvent(const QString &input, int element,
										Node *output) override;

private:
	TrackList *get_track_list() const;

	Sequence *sequence_;
};

}

#endif // OAK_MULTICAMNODE_H
