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

#ifndef OAK_PLAYBACKCACHE_H
#define OAK_PLAYBACKCACHE_H

#include <olive/core/core.h>
#include <QDir>
#include <QMutex>
#include <QObject>
#include <QPainter>
#include <QUuid>

#include "common/jobtime.h"

using namespace olive::core;

namespace olive
{

class Node;
class Project;
class ViewerOutput;

class PlaybackCache : public QObject {
	Q_OBJECT
public:
	PlaybackCache(QObject *parent = nullptr);

	const QUuid &get_uuid() const
	{
		return uuid_;
	}
	void set_uuid(const QUuid &u);

	TimeRangeList get_invalidated_ranges(TimeRange intersecting) const;
	TimeRangeList get_invalidated_ranges(const Rational &length) const
	{
		return get_invalidated_ranges(TimeRange(0, length));
	}

	bool has_invalidated_ranges(const TimeRange &intersecting) const;
	bool has_invalidated_ranges(const Rational &length) const
	{
		return has_invalidated_ranges(TimeRange(0, length));
	}

	QString get_cache_directory() const;

	void invalidate(const TimeRange &r);

	bool has_validated_ranges() const
	{
		return !validated_.isEmpty();
	}
	const TimeRangeList &get_validated_ranges() const
	{
		return validated_;
	}

	Node *parent() const;

	QDir get_this_cache_directory() const;
	static QDir get_this_cache_directory(const QString &cache_path,
									  const QUuid &cache_id);

	void load_state();
	void save_state();

	void draw(QPainter *painter, const Rational &start, double scale,
			  const QRect &rect) const;

	static int get_cache_indicator_height()
	{
		return QFontMetrics(QFont()).height() / 4;
	}

	bool is_saving_enabled() const
	{
		return saving_enabled_;
	}
	void set_saving_enabled(bool e)
	{
		saving_enabled_ = e;
	}

	virtual void set_passthrough(PlaybackCache *cache);

	QMutex *mutex()
	{
		return &mutex_;
	}

	class Passthrough : public TimeRange {
	public:
		Passthrough(const TimeRange &r)
			: TimeRange(r)
		{
		}

		QUuid cache;
	};

	const std::vector<Passthrough> &get_passthroughs() const
	{
		return passthroughs_;
	}

	void clear_request_range(const TimeRange &r)
	{
		requested_.remove(r);
	}

	void resignal_requests()
	{
		for (const TimeRange &r : requested_) {
			emit requested(request_context_, r);
		}
	}

public slots:
	void invalidate_all();

	void request(ViewerOutput *context, const TimeRange &r);

signals:
	void invalidated(const TimeRange &r);

	void validated(const TimeRange &r);

	void requested(ViewerOutput *context, const TimeRange &r);

	void cancel_all();

protected:
	void validate(const TimeRange &r, bool signal = true);

	virtual void InvalidateEvent(const TimeRange &range);

	virtual void LoadStateEvent(QDataStream &stream)
	{
	}

	virtual void SaveStateEvent(QDataStream &stream)
	{
	}

	Project *get_project() const;

private:
	TimeRangeList validated_;

	TimeRangeList requested_;
	ViewerOutput *request_context_;

	QUuid uuid_;

	bool saving_enabled_;

	QMutex mutex_;

	std::vector<Passthrough> passthroughs_;

	qint64 last_loaded_state_;
};

}

#endif // OAK_PLAYBACKCACHE_H
