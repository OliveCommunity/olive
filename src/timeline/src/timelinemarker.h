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

#ifndef OAK_TIMELINEMARKER_H
#define OAK_TIMELINEMARKER_H

#include <memory>
#include <string>
#include <vector>

#include <olive/core/core.h>

#include "undocommand.h"
#include "xmlutils.h"

using namespace olive::core;

namespace olive
{

class TimelineMarkerList;

/**
 * @brief A marker on a timeline, with a time range, a name and a color
 *
 * De-Qt version: no QObject, no signals. Modification notifications are
 * emitted by the caller's layer (facade). The draw() helper moved to the
 * app layer.
 */
class TimelineMarker {
public:
	TimelineMarker();
	TimelineMarker(int color, const TimeRange &time,
				   const std::string &name = std::string());

	const TimeRange &time() const
	{
		return time_;
	}
	void set_time(const TimeRange &time);
	void set_time(const Rational &time);

	bool has_sibling_at_time(const Rational &t) const;

	const std::string &name() const
	{
		return name_;
	}
	void set_name(const std::string &name);

	int color() const
	{
		return color_;
	}
	void set_color(int c);

	bool load(XmlStreamReader *reader);
	void save(XmlStreamWriter *writer) const;

private:
	TimeRange time_;

	std::string name_;

	int color_;

	TimelineMarkerList *parent_;

	friend class TimelineMarkerList;
};

class TimelineMarkerList {
public:
	TimelineMarkerList() = default;

	inline bool empty() const
	{
		return markers_.empty();
	}
	inline size_t size() const
	{
		return markers_.size();
	}
	inline TimelineMarker *at(size_t i) const
	{
		return markers_[i].get();
	}
	inline TimelineMarker *back() const
	{
		return markers_.back().get();
	}
	inline TimelineMarker *front() const
	{
		return markers_.front().get();
	}

	bool load(XmlStreamReader *reader);
	void save(XmlStreamWriter *writer) const;

	/**
	 * @brief Insert a marker, keeping the list sorted by time
	 *
	 * Takes ownership. Replaces the old QObject childEvent
	 * auto-registration.
	 */
	void add_marker(std::unique_ptr<TimelineMarker> marker);

	/**
	 * @brief Detach a marker from the list, returning ownership
	 *
	 * Returns nullptr if the marker is not in the list.
	 */
	std::unique_ptr<TimelineMarker> remove_marker(TimelineMarker *m);

	TimelineMarker *get_marker_at_time(const Rational &t) const
	{
		for (const auto &m : markers_) {
			if (m->time().in() == t) {
				return m.get();
			}
		}

		return nullptr;
	}

	TimelineMarker *get_closest_marker_to_time(const Rational &t) const
	{
		TimelineMarker *closest = nullptr;

		for (const auto &m : markers_) {
			Rational this_diff = rational_abs(m->time().in() - t);

			if (closest) {
				Rational stored_diff = rational_abs(closest->time().in() - t);

				if (this_diff > stored_diff) {
					// Since the list is organized by time, if the diff
					// increases, assume we are only going to move further
					// away from here and there's no need to check
					break;
				}
			}

			closest = m.get();
		}

		return closest;
	}

	/**
	 * @brief Re-sort a marker after its time changed
	 *
	 * Formerly triggered by the marker's time_changed signal; now called
	 * directly by TimelineMarker::set_time().
	 */
	void resort(TimelineMarker *m);

private:
	static Rational rational_abs(const Rational &r)
	{
		return r < Rational(0) ? -r : r;
	}

	void insert_into_list(std::unique_ptr<TimelineMarker> marker);

	std::vector<std::unique_ptr<TimelineMarker>> markers_;
};

class MarkerAddCommand : public UndoCommand {
public:
	MarkerAddCommand(TimelineMarkerList *marker_list, const TimeRange &range,
					 const std::string &name, int color);
	MarkerAddCommand(TimelineMarkerList *marker_list,
					 std::unique_ptr<TimelineMarker> marker);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	TimelineMarkerList *marker_list_;

	TimelineMarker *added_marker_;
	std::unique_ptr<TimelineMarker> memory_manager_;
};

class MarkerRemoveCommand : public UndoCommand {
public:
	MarkerRemoveCommand(TimelineMarker *marker, TimelineMarkerList *marker_list);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	TimelineMarker *marker_;
	TimelineMarkerList *marker_list_;

	std::unique_ptr<TimelineMarker> memory_manager_;
};

class MarkerChangeColorCommand : public UndoCommand {
public:
	MarkerChangeColorCommand(TimelineMarker *marker, int new_color);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	TimelineMarker *marker_;
	int old_color_;
	int new_color_;
};

class MarkerChangeNameCommand : public UndoCommand {
public:
	MarkerChangeNameCommand(TimelineMarker *marker, std::string name);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	TimelineMarker *marker_;
	std::string old_name_;
	std::string new_name_;
};

class MarkerChangeTimeCommand : public UndoCommand {
public:
	MarkerChangeTimeCommand(TimelineMarker *marker, const TimeRange &time,
							const TimeRange &old_time);
	MarkerChangeTimeCommand(TimelineMarker *marker, const TimeRange &time)
		: MarkerChangeTimeCommand(marker, time, marker->time())
	{
	}

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	TimelineMarker *marker_;
	TimeRange old_time_;
	TimeRange new_time_;
};

}

#endif // OAK_TIMELINEMARKER_H
