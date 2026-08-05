#pragma once
#include <string>
#include "xmlutils.h"
#include "olive/core/util/timerange.h"
namespace olive {
using core::TimeRange;
class TimelineMarkerList {
public:
	template <typename T> explicit TimelineMarkerList(T *) {}
	bool load(XmlStreamReader *) { return true; }
	void save(XmlStreamWriter *) const {}
};
class TimelineMarker {
public:
	TimelineMarker() {}
	explicit TimelineMarker(TimelineMarkerList *) {}
	TimelineMarker(int, const TimeRange &, const std::string &,
				   TimelineMarkerList *) {}
	void set_name(const std::string &) {}
	void set_color(int) {}
	void set_time(const TimeRange &) {}
	bool load(XmlStreamReader *) { return true; }
	void save(XmlStreamWriter *) const {}
};
}
