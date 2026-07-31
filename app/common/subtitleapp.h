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

#ifndef OAK_SUBTITLEAPP_H
#define OAK_SUBTITLEAPP_H

#include <QString>
#include <QVariant>

#include <olive/core/util/timerange.h>

namespace olive
{

using olive::core::TimeRange;

/**
 * @brief App-side mirror of the engine Subtitle value class
 * (engine/render/subtitleparams.h).
 *
 * Pure value type (time range + text), identical semantics to the engine
 * version. It is named SubtitleApp (not Subtitle) because the engine class
 * still reaches some app translation units transitively (e.g. via
 * engine/node/output/viewer/viewer.h) and an identical name would be an
 * ODR redefinition there.
 *
 * The member layout MUST stay in sync with the engine class:
 * oakengine_viewer_get_subtitle_at() returns pointers to engine Subtitle
 * objects which the app reads through this mirror. Update both sides
 * together.
 */
class SubtitleApp {
public:
	SubtitleApp() = default;

	SubtitleApp(const TimeRange &time, const QString &text)
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

	const QString &text() const
	{
		return text_;
	}
	void set_text(const QString &t)
	{
		text_ = t;
	}

private:
	TimeRange range_;

	QString text_;
};

} // namespace olive

Q_DECLARE_METATYPE(olive::SubtitleApp)

#endif // OAK_SUBTITLEAPP_H
