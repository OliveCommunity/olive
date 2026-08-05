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

#include "timeformat.h"

#include <cstdio>
#include <ctime>

namespace olive
{

#define super Node

const std::string TimeFormatNode::k_time_input = "time_in";
const std::string TimeFormatNode::k_format_input = "format_in";
const std::string TimeFormatNode::k_local_time_input = "localtime_in";

namespace {

/**
 * @brief Expand Qt date/time format tokens (QDateTime::toString syntax)
 *
 * Supports the field tokens d/dd, M/MM, yy/yyyy, h/hh, H/HH, m/mm, s/ss,
 * z/zz/zzz, AP/ap/A/a, and single-quoted literal sections, mirroring Qt's
 * longest-run matching.
 */
std::string format_date_time(const std::tm &tm, int ms, const std::string &format)
{
	// Qt displays h/hh on the 12-hour clock only when the format contains an
	// AM/PM token (AP, ap, A or a); otherwise it is the 24-hour clock
	bool has_am_pm = format.find("AP") != std::string::npos ||
					 format.find("ap") != std::string::npos ||
					 format.find('A') != std::string::npos ||
					 format.find('a') != std::string::npos;

	std::string out;
	for (size_t i = 0; i < format.size();) {
		char c = format[i];
		if (c == '\'') {
			size_t end = format.find('\'', i + 1);
			if (end == std::string::npos) {
				out.append(format, i + 1, std::string::npos);
				break;
			}
			out.append(format, i + 1, end - (i + 1));
			i = end + 1;
			continue;
		}
		size_t run = 1;
		while (i + run < format.size() && format[i + run] == c) {
			run++;
		}
		char buf[16];
		switch (c) {
		case 'd':
			snprintf(buf, sizeof(buf), run >= 2 ? "%02d" : "%d", tm.tm_mday);
			out += buf;
			break;
		case 'M':
			snprintf(buf, sizeof(buf), run >= 2 ? "%02d" : "%d", tm.tm_mon + 1);
			out += buf;
			break;
		case 'y':
			if (run >= 4) {
				snprintf(buf, sizeof(buf), "%04d", tm.tm_year + 1900);
			} else {
				snprintf(buf, sizeof(buf), "%02d", (tm.tm_year + 1900) % 100);
			}
			out += buf;
			break;
		case 'H':
			snprintf(buf, sizeof(buf), run >= 2 ? "%02d" : "%d", tm.tm_hour);
			out += buf;
			break;
		case 'h': {
			int hour = tm.tm_hour;
			if (has_am_pm) {
				hour %= 12;
				if (hour == 0) {
					hour = 12;
				}
			}
			snprintf(buf, sizeof(buf), run >= 2 ? "%02d" : "%d", hour);
			out += buf;
			break;
		}
		case 'm':
			snprintf(buf, sizeof(buf), run >= 2 ? "%02d" : "%d", tm.tm_min);
			out += buf;
			break;
		case 's':
			snprintf(buf, sizeof(buf), run >= 2 ? "%02d" : "%d", tm.tm_sec);
			out += buf;
			break;
		case 'z':
			snprintf(buf, sizeof(buf), run >= 3 ? "%03d" : "%d", ms);
			out += buf;
			break;
		case 'A':
		case 'a': {
			// Qt: A/AP/ap/a are all replaced by the full AM/PM string; the
			// two-letter form is a single token
			if (i + run < format.size() && format[i + run] == char(c + ('P' - 'A'))) {
				run++;
			}
			const char *am = (c == 'A') ? "AM" : "am";
			const char *pm = (c == 'A') ? "PM" : "pm";
			out += (tm.tm_hour < 12) ? am : pm;
			break;
		}
		default:
			out.append(format, i, run);
			break;
		}
		i += run;
	}
	return out;
}

}

TimeFormatNode::TimeFormatNode()
{
	add_input(k_time_input, NodeValue::k_float);
	add_input(k_format_input, NodeValue::k_text, "hh:mm:ss");
	add_input(k_local_time_input, NodeValue::k_boolean);
}

std::string TimeFormatNode::name() const
{
	return "Time Format";
}

std::string TimeFormatNode::id() const
{
	return "org.olivevideoeditor.Olive.timeformat";
}

std::vector<Node::CategoryID> TimeFormatNode::category() const
{
	return { k_category_generator };
}

std::string TimeFormatNode::description() const
{
	return "Format time (in Unix epoch seconds) into a string.";
}

void TimeFormatNode::retranslate()
{
	super::retranslate();

	set_input_name(k_time_input, "Time");
	set_input_name(k_format_input, "Format");
	set_input_name(k_local_time_input, "Interpret time as local time");
}

void TimeFormatNode::value(const NodeValueRow &value,
						   const NodeGlobals &globals,
						   NodeValueTable *table) const
{
	int64_t ms_since_epoch = value.at(k_time_input).to_double() * 1000;
	bool time_is_local = value.at(k_local_time_input).to_bool();
	std::time_t secs = std::time_t(ms_since_epoch / 1000);
	int ms = int(ms_since_epoch % 1000);
	std::tm tm;
	if (time_is_local) {
		localtime_r(&secs, &tm);
	} else {
		gmtime_r(&secs, &tm);
	}
	std::string format = value.at(k_format_input).to_string();
	std::string output = format_date_time(tm, ms, format);
	table->push(NodeValue(NodeValue::k_text, output, this));
}

}
