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

#ifndef OAK_DYNLIB_H
#define OAK_DYNLIB_H

// Minimal QLibrary replacement for loading render backend shared libraries.

#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace olive
{

class DynLib {
public:
	DynLib() = default;

	~DynLib()
	{
		unload();
	}

	DynLib(const DynLib &) = delete;
	DynLib &operator=(const DynLib &) = delete;

	void set_file_name(const std::string &path)
	{
		unload();
		path_ = path;
	}

	const std::string &file_name() const
	{
		return path_;
	}

	bool load()
	{
#if defined(_WIN32)
		handle_ = LoadLibraryA(path_.c_str());
		if (!handle_) {
			error_string_ = "LoadLibrary failed";
		}
#else
		handle_ = dlopen(path_.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!handle_) {
			const char *err = dlerror();
			error_string_ = err ? err : "dlopen failed";
		}
#endif
		return handle_ != nullptr;
	}

	bool unload()
	{
		if (!handle_) {
			return true;
		}
#if defined(_WIN32)
		const bool ok = FreeLibrary(HMODULE(handle_)) != 0;
#else
		const bool ok = dlclose(handle_) == 0;
#endif
		handle_ = nullptr;
		return ok;
	}

	bool is_loaded() const
	{
		return handle_ != nullptr;
	}

	void *resolve(const char *symbol)
	{
		if (!handle_) {
			return nullptr;
		}
#if defined(_WIN32)
		return reinterpret_cast<void *>(
			GetProcAddress(HMODULE(handle_), symbol));
#else
		return dlsym(handle_, symbol);
#endif
	}

	std::string error_string() const
	{
		return error_string_;
	}

private:
	std::string path_;
	void *handle_ = nullptr;
	std::string error_string_;
};

}

#endif // OAK_DYNLIB_H
