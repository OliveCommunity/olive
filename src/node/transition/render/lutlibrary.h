#pragma once
// Transitional stub for engine/render/lutlibrary.h (still Qt-based). Only the
// surface oaknode uses. M7 replaces this with the real oakrender boundary.
#include "variant.h"
namespace olive {
class LUTLibrary {
public:
	static const StringList &supported_extensions()
	{
		static const StringList e;
		return e;
	}
	static bool is_supported_extension(const std::string &) { return false; }
};
}
