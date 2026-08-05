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

#ifndef OAK_SUBTITLEPARAMS_H
#define OAK_SUBTITLEPARAMS_H

#include <string>
#include <vector>

#include <olive/core/util/timerange.h>

#include "xmlutils.h"

using namespace olive::core;

namespace olive
{

/**
 * @brief A single subtitle entry (sunk from engine/render/subtitleparams.h,
 *        QString replaced with std::string).
 */
class Subtitle {
public:
	Subtitle() = default;

	Subtitle(const TimeRange &time, const std::string &text)
		: range_(time)
		, text_(text)
	{
	}

	const TimeRange &time() const
	{
		return range_;
	}
	void set_time(const TimeRange &t)
	{
		range_ = t;
	}

	const std::string &text() const
	{
		return text_;
	}
	void set_text(const std::string &t)
	{
		text_ = t;
	}

	// Value equality (time range + text). Required by olive::Variant's
	// custom-type operator== (SubtitleParams is stored as a Variant value).
	bool operator==(const Subtitle &rhs) const
	{
		return range_ == rhs.range_ && text_ == rhs.text_;
	}
	bool operator!=(const Subtitle &rhs) const
	{
		return !(*this == rhs);
	}

private:
	TimeRange range_;

	std::string text_;
};

/**
 * @brief Subtitle track parameter set (sunk from engine/render/subtitleparams.h).
 */
class SubtitleParams : public std::vector<Subtitle> {
public:
	SubtitleParams()
	{
		stream_index_ = 0;
		enabled_ = true;
	}

	static std::string generate_ass_header();

	void load(XmlStreamReader *reader);

	void save(XmlStreamWriter *writer) const;

	bool is_valid() const
	{
		return !this->empty();
	}

	Rational duration() const
	{
		if (this->empty()) {
			return 0;
		} else {
			return back().time().out();
		}
	}

	int stream_index() const
	{
		return stream_index_;
	}
	void set_stream_index(int i)
	{
		stream_index_ = i;
	}

	bool enabled() const
	{
		return enabled_;
	}
	void set_enabled(bool e)
	{
		enabled_ = e;
	}

private:
	int stream_index_;

	bool enabled_;
};

}

#endif // OAK_SUBTITLEPARAMS_H
