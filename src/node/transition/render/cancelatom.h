#pragma once
namespace olive {
class CancelAtom {
public:
	bool is_cancelled() const { return false; }
	bool heard_cancel() const { return false; }
};
}
