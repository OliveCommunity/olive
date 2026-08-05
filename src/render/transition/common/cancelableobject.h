#pragma once
// Transitional: engine/common/cancelableobject.h de-Qt target form. Header-only
// in the original; the only Qt dependency was the transitively included
// render/cancelatom.h, which oakrender provides for real. M-common folds this
// into oakcommon; until then this contract matches the original 1:1.
#include "cancelatom.h"

namespace olive {

class CancelableObject {
public:
	CancelableObject() {}

	virtual ~CancelableObject() = default;

	void cancel()
	{
		cancel_.cancel();
		CancelEvent();
	}

	CancelAtom *get_cancel_atom() { return &cancel_; }

	bool is_cancelled() { return cancel_.is_cancelled(); }

protected:
	virtual void CancelEvent() {}

private:
	CancelAtom cancel_;
};

}
