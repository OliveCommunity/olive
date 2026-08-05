#pragma once
// Transitional: engine/common/jobtime.h de-Qt target form. Original kept a
// process-global mutex-protected counter; the atomic below preserves the same
// monotonic-unique semantics. oakcommon has no jobtime.h yet (M-common folds
// it in); renderjobtracker.h includes "common/jobtime.h".
#include <atomic>
#include <cstdint>

namespace olive {

class JobTime {
public:
	JobTime() { acquire(); }

	void acquire()
	{
		static std::atomic<uint64_t> index{ 0 };
		value_ = index.fetch_add(1, std::memory_order_relaxed);
	}

	uint64_t value() const { return value_; }

	bool operator==(const JobTime &rhs) const { return value_ == rhs.value_; }
	bool operator!=(const JobTime &rhs) const { return value_ != rhs.value_; }
	bool operator<(const JobTime &rhs) const { return value_ < rhs.value_; }
	bool operator>(const JobTime &rhs) const { return value_ > rhs.value_; }
	bool operator<=(const JobTime &rhs) const { return value_ <= rhs.value_; }
	bool operator>=(const JobTime &rhs) const { return value_ >= rhs.value_; }

private:
	uint64_t value_;
};

}
