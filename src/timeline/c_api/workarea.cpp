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

#include "timeline/workarea.h"

#include <new>

#include "../src/timelineundoworkarea.h"
#include "../src/timelineworkarea.h"
#include "../../undo/c_api/commandhandle.h"

namespace
{

olive::TimelineWorkArea *impl(OakTimelineWorkArea *h)
{
	return reinterpret_cast<olive::TimelineWorkArea *>(h);
}

const olive::TimelineWorkArea *impl(const OakTimelineWorkArea *h)
{
	return reinterpret_cast<const olive::TimelineWorkArea *>(h);
}

OakUndoCommand *wrap_command(olive::UndoCommand *command)
{
	if (!command) {
		return NULL;
	}
	OakUndoCommand *handle = new (std::nothrow) OakUndoCommand{command, true};
	if (!handle) {
		delete command;
	}
	return handle;
}

olive::core::Rational rat(int64_t n, int64_t d)
{
	return olive::core::Rational(int(n), int(d));
}

} // namespace

OakTimelineWorkArea *oaktimeline_workarea_of(OakNodeNode *owner)
{
	if (!owner) {
		return NULL;
	}

	OakNodeWorkArea *workarea = NULL;
	if (oaknode_node_get_work_area(owner, &workarea) != OAKNODE_OK) {
		return NULL;
	}
	return reinterpret_cast<OakTimelineWorkArea *>(workarea);
}

int oaktimeline_workarea_get(const OakTimelineWorkArea *w, int *in_num,
							 int *in_den, int *out_num, int *out_den,
							 int *enabled)
{
	if (!w) {
		return OAKTIMELINE_E_INVALID;
	}

	if (in_num) {
		*in_num = impl(w)->in().numerator();
	}
	if (in_den) {
		*in_den = impl(w)->in().denominator();
	}
	if (out_num) {
		*out_num = impl(w)->out().numerator();
	}
	if (out_den) {
		*out_den = impl(w)->out().denominator();
	}
	if (enabled) {
		*enabled = impl(w)->enabled() ? 1 : 0;
	}
	return OAKTIMELINE_OK;
}

int oaktimeline_workarea_set_range(OakTimelineWorkArea *w, int in_num,
								   int in_den, int out_num, int out_den)
{
	if (!w) {
		return OAKTIMELINE_E_INVALID;
	}

	try {
		impl(w)->set_range(
			olive::core::TimeRange(rat(in_num, in_den), rat(out_num, out_den)));
		return OAKTIMELINE_OK;
	} catch (...) {
		return OAKTIMELINE_E_FAILED;
	}
}

OakUndoCommand *oaktimeline_workarea_set_range_command(
	OakTimelineWorkArea *w, int in_num, int in_den, int out_num,
	int out_den, int old_in_num, int old_in_den, int old_out_num,
	int old_out_den)
{
	if (!w) {
		return NULL;
	}

	try {
		olive::core::TimeRange range(rat(in_num, in_den), rat(out_num, out_den));
		olive::core::TimeRange old_range(rat(old_in_num, old_in_den),
										 rat(old_out_num, old_out_den));
		return wrap_command(
			new olive::WorkareaSetRangeCommand(impl(w), range, old_range));
	} catch (...) {
		return NULL;
	}
}

OakUndoCommand *oaktimeline_workarea_set_enabled_command(
	OakTimelineWorkArea *w, int enabled)
{
	if (!w) {
		return NULL;
	}

	try {
		return wrap_command(
			new olive::WorkareaSetEnabledCommand(impl(w), enabled != 0));
	} catch (...) {
		return NULL;
	}
}

int oaktimeline_workarea_reset(int *in_num, int *in_den, int *out_num,
							   int *out_den)
{
	if (!in_num || !in_den || !out_num || !out_den) {
		return OAKTIMELINE_E_INVALID;
	}

	*in_num = olive::TimelineWorkArea::k_reset_in.numerator();
	*in_den = olive::TimelineWorkArea::k_reset_in.denominator();
	*out_num = olive::TimelineWorkArea::k_reset_out.numerator();
	*out_den = olive::TimelineWorkArea::k_reset_out.denominator();
	return OAKTIMELINE_OK;
}

int oaktimeline_workarea_load(OakTimelineWorkArea *w, OakXmlReader reader)
{
	if (!w || !reader.ctx) {
		return OAKTIMELINE_E_INVALID;
	}

	olive::XmlStreamReader *native = oakcommon_xml_reader_get_native(reader);
	if (!native) {
		return OAKTIMELINE_E_INVALID;
	}

	try {
		return impl(w)->load(native) ? OAKTIMELINE_OK : OAKTIMELINE_E_FAILED;
	} catch (...) {
		return OAKTIMELINE_E_FAILED;
	}
}

int oaktimeline_workarea_save(const OakTimelineWorkArea *w,
							  OakXmlWriter writer)
{
	if (!w || !writer.ctx) {
		return OAKTIMELINE_E_INVALID;
	}

	olive::XmlStreamWriter *native = oakcommon_xml_writer_get_native(writer);
	if (!native) {
		return OAKTIMELINE_E_INVALID;
	}

	try {
		impl(w)->save(native);
		return OAKTIMELINE_OK;
	} catch (...) {
		return OAKTIMELINE_E_FAILED;
	}
}
