#pragma once
#include "xmlutils.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"
namespace olive {
using core::Rational;
using core::TimeRange;
class TimelineWorkArea {
public:
	template <typename T> explicit TimelineWorkArea(T *) {}
	const Rational &in() const { return in_; }
	const Rational &out() const { return out_; }
	void set_enabled(bool) {}
	void set_range(const TimeRange &) {}
	TimeRange range() const { return TimeRange(); }
	bool load(XmlStreamReader *) { return true; }
	void save(XmlStreamWriter *) const {}
private:
	Rational in_, out_;
};
}
