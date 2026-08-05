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

#include "common/subtitleparams.h"

#include <cstring>

#include "../src/subtitleparams.h"

struct OakCommonSubtitleParams {
	olive::SubtitleParams impl;
};

namespace
{

int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = (int)value.size() + 1;
	if (buf && buf_size >= needed)
		memcpy(buf, value.c_str(), needed);
	return needed;
}

} // namespace

OakCommonSubtitleParams *oakcommon_subtitleparams_init(void)
{
	try {
		return new OakCommonSubtitleParams{olive::SubtitleParams()};
	} catch (...) {
		return nullptr;
	}
}

void oakcommon_subtitleparams_free(OakCommonSubtitleParams *params)
{
	delete params;
}

int oakcommon_subtitleparams_get_stream_index(
	OakCommonSubtitleParams *params, int *index)
{
	if (!params || !index)
		return OAKCOMMON_E_INVALID;
	*index = params->impl.stream_index();
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_set_stream_index(
	OakCommonSubtitleParams *params, int index)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_stream_index(index);
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_get_enabled(OakCommonSubtitleParams *params,
										 int *enabled)
{
	if (!params || !enabled)
		return OAKCOMMON_E_INVALID;
	*enabled = params->impl.enabled() ? 1 : 0;
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_set_enabled(OakCommonSubtitleParams *params,
										 int enabled)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_enabled(enabled != 0);
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_is_valid(OakCommonSubtitleParams *params,
									  int *is_valid)
{
	if (!params || !is_valid)
		return OAKCOMMON_E_INVALID;
	*is_valid = params->impl.is_valid() ? 1 : 0;
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_count(OakCommonSubtitleParams *params, int *count)
{
	if (!params || !count)
		return OAKCOMMON_E_INVALID;
	*count = (int)params->impl.size();
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_duration(OakCommonSubtitleParams *params,
									  int *numerator, int *denominator)
{
	if (!params || !numerator || !denominator)
		return OAKCOMMON_E_INVALID;
	olive::core::Rational d = params->impl.duration();
	*numerator = d.numerator();
	*denominator = d.denominator();
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_add_subtitle(OakCommonSubtitleParams *params,
										  int in_num, int in_den, int out_num,
										  int out_den, const char *text)
{
	if (!params || !text)
		return OAKCOMMON_E_INVALID;
	try {
		params->impl.push_back(olive::Subtitle(
			olive::core::TimeRange(olive::core::Rational(in_num, in_den),
								   olive::core::Rational(out_num, out_den)),
			text));
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_subtitleparams_clear(OakCommonSubtitleParams *params)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.clear();
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_get_subtitle(OakCommonSubtitleParams *params,
										  int index, int *in_num, int *in_den,
										  int *out_num, int *out_den)
{
	if (!params || !in_num || !in_den || !out_num || !out_den)
		return OAKCOMMON_E_INVALID;
	if (index < 0 || index >= (int)params->impl.size())
		return OAKCOMMON_E_NOT_FOUND;
	const olive::Subtitle &s = params->impl.at(index);
	olive::core::Rational in = s.time().in();
	olive::core::Rational out = s.time().out();
	*in_num = in.numerator();
	*in_den = in.denominator();
	*out_num = out.numerator();
	*out_den = out.denominator();
	return OAKCOMMON_OK;
}

int oakcommon_subtitleparams_get_subtitle_text(OakCommonSubtitleParams *params,
											   int index, char *buf,
											   int buf_size)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	if (index < 0 || index >= (int)params->impl.size())
		return OAKCOMMON_E_NOT_FOUND;
	try {
		return copy_string(params->impl.at(index).text(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_subtitleparams_generate_ass_header(char *buf, int buf_size)
{
	try {
		return copy_string(olive::SubtitleParams::generate_ass_header(), buf,
						   buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_subtitleparams_load_xml(OakCommonSubtitleParams *params,
									  const char *xml)
{
	if (!params || !xml)
		return OAKCOMMON_E_INVALID;
	try {
		olive::XmlStreamReader reader(xml);
		if (reader.has_error())
			return OAKCOMMON_E_FAILED;
		// Position on the root element; load() consumes its children.
		if (!olive::xml_read_next_start_element(&reader))
			return OAKCOMMON_E_FAILED;
		params->impl.load(&reader);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_subtitleparams_save_xml(OakCommonSubtitleParams *params,
									  char *buf, int buf_size)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	try {
		olive::XmlStreamWriter writer;
		writer.write_start_element("subtitleparams");
		params->impl.save(&writer);
		writer.write_end_element();
		return copy_string(writer.output(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
