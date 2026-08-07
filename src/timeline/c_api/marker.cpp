/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "timeline/marker.h"

#include <new>

#include "../src/timelinemarker.h"
#include "../../undo/c_api/commandhandle.h"

namespace
{

olive::TimelineMarkerList *impl(OakTimelineMarkerList *h)
{
	return reinterpret_cast<olive::TimelineMarkerList *>(h);
}

const olive::TimelineMarkerList *impl(const OakTimelineMarkerList *h)
{
	return reinterpret_cast<const olive::TimelineMarkerList *>(h);
}

OakTimelineMarkerList *wrap(olive::TimelineMarkerList *l)
{
	return reinterpret_cast<OakTimelineMarkerList *>(l);
}

OakUndoCommand wrap_command(olive::UndoCommand *command)
{
	if (!command) {
		return OakUndoCommand{};
	}
	return oakundo_capi::make_command_handle(command, true);
}

olive::core::Rational rat(int n, int d)
{
	return olive::core::Rational(n, d);
}

olive::TimelineMarker *marker_at(OakTimelineMarkerList *list, int index)
{
	if (!list || index < 0 || index >= int(impl(list)->size())) {
		return nullptr;
	}
	return impl(list)->at(index);
}

} // namespace

OakTimelineMarkerList *oaktimeline_marker_list_of(OakNodeNode *owner)
{
	if (!owner) {
		return NULL;
	}

	OakNodeMarkerList *markers = NULL;
	if (oaknode_node_get_markers(owner, &markers) != OAKNODE_OK) {
		return NULL;
	}
	return reinterpret_cast<OakTimelineMarkerList *>(markers);
}

int oaktimeline_marker_count(const OakTimelineMarkerList *list,
							 int *out_count)
{
	if (!list || !out_count) {
		return OAKTIMELINE_E_INVALID;
	}
	*out_count = int(impl(list)->size());
	return OAKTIMELINE_OK;
}

int oaktimeline_marker_at(const OakTimelineMarkerList *list, int index,
						  int *in_num, int *in_den, int *out_num, int *out_den,
						  int *color, char *name_buf, int buf_size)
{
	if (!list) {
		return OAKTIMELINE_E_INVALID;
	}
	if (index < 0 || index >= int(impl(list)->size())) {
		return OAKTIMELINE_E_NOT_FOUND;
	}

	const olive::TimelineMarker *m = impl(list)->at(index);

	if (in_num) {
		*in_num = m->time().in().numerator();
	}
	if (in_den) {
		*in_den = m->time().in().denominator();
	}
	if (out_num) {
		*out_num = m->time().out().numerator();
	}
	if (out_den) {
		*out_den = m->time().out().denominator();
	}
	if (color) {
		*color = m->color();
	}

	const std::string &name = m->name();
	int needed = int(name.size()) + 1;
	if (name_buf && buf_size >= needed) {
		memcpy(name_buf, name.c_str(), needed);
	}
	return needed;
}

OakUndoCommand oaktimeline_marker_add_command(
	OakTimelineMarkerList *list, int in_num, int in_den, int out_num,
	int out_den, const char *name, int color)
{
	if (!list) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(new olive::MarkerAddCommand(
			impl(list),
			olive::core::TimeRange(rat(int(in_num), int(in_den)),
								   rat(int(out_num), int(out_den))),
			name ? name : "", color));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_marker_remove_at_command(
	OakTimelineMarkerList *list, int index)
{
	olive::TimelineMarker *m = marker_at(list, index);
	if (!m) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(new olive::MarkerRemoveCommand(m, impl(list)));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_marker_set_time_command(
	OakTimelineMarkerList *list, int index, int in_num, int in_den,
	int out_num, int out_den)
{
	olive::TimelineMarker *m = marker_at(list, index);
	if (!m) {
		return OakUndoCommand{};
	}

	try {
		return wrap_command(new olive::MarkerChangeTimeCommand(
			m, olive::core::TimeRange(rat(int(in_num), int(in_den)),
									  rat(int(out_num), int(out_den)))));
	} catch (...) {
		return OakUndoCommand{};
	}
}

OakUndoCommand oaktimeline_marker_set_props_command(
	OakTimelineMarkerList *list, int index, int color, const char *name)
{
	olive::TimelineMarker *m = marker_at(list, index);
	if (!m || (color < 0 && !name)) {
		return OakUndoCommand{};
	}

	try {
		if (color >= 0 && name) {
			auto *multi = new olive::MultiUndoCommand();
			multi->add_child(new olive::MarkerChangeColorCommand(m, color));
			multi->add_child(new olive::MarkerChangeNameCommand(m, name));
			return wrap_command(multi);
		}
		if (color >= 0) {
			return wrap_command(new olive::MarkerChangeColorCommand(m, color));
		}
		return wrap_command(new olive::MarkerChangeNameCommand(m, name));
	} catch (...) {
		return OakUndoCommand{};
	}
}

int oaktimeline_marker_list_load(OakTimelineMarkerList *list,
								 OakXmlReader reader)
{
	if (!list || !reader.ctx) {
		return OAKTIMELINE_E_INVALID;
	}

	olive::XmlStreamReader *native = oakcommon_xml_reader_get_native(reader);
	if (!native) {
		return OAKTIMELINE_E_INVALID;
	}

	try {
		return impl(list)->load(native) ? OAKTIMELINE_OK
										: OAKTIMELINE_E_FAILED;
	} catch (...) {
		return OAKTIMELINE_E_FAILED;
	}
}

int oaktimeline_marker_list_save(const OakTimelineMarkerList *list,
								 OakXmlWriter writer)
{
	if (!list || !writer.ctx) {
		return OAKTIMELINE_E_INVALID;
	}

	olive::XmlStreamWriter *native = oakcommon_xml_writer_get_native(writer);
	if (!native) {
		return OAKTIMELINE_E_INVALID;
	}

	try {
		impl(list)->save(native);
		return OAKTIMELINE_OK;
	} catch (...) {
		return OAKTIMELINE_E_FAILED;
	}
}
