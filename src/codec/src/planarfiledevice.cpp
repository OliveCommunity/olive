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

#include "planarfiledevice.h"

#include <sys/stat.h>

namespace olive
{

PlanarFileDevice::PlanarFileDevice() = default;

PlanarFileDevice::~PlanarFileDevice()
{
	close();
}

bool PlanarFileDevice::open(const std::vector<std::string> &filenames,
							OpenMode mode)
{
	if (isOpen()) {
		// Already open
		return false;
	}

	const char *mode_str = (mode == k_read_only) ? "rb" : "wb";

	files_.resize(filenames.size(), nullptr);

	for (size_t i = 0; i < files_.size(); i++) {
		files_[i] = std::fopen(filenames.at(i).c_str(), mode_str);
		if (!files_[i]) {
			close();
			return false;
		}
	}

	return true;
}

int64_t PlanarFileDevice::read(char **data, int64_t bytes_per_channel,
							   int64_t offset)
{
	int64_t ret = -1;

	if (isOpen()) {
		for (size_t i = 0; i < files_.size(); i++) {
			// Kind of clunky but should be largely fine
			ret = int64_t(std::fread(data[i] + offset, 1,
									 size_t(bytes_per_channel), files_[i]));
		}
	}

	return ret;
}

int64_t PlanarFileDevice::write(const char **data, int64_t bytes_per_channel,
								int64_t offset)
{
	int64_t ret = -1;

	if (isOpen()) {
		for (size_t i = 0; i < files_.size(); i++) {
			// Kind of clunky but should be largely fine
			ret = int64_t(std::fwrite(data[i] + offset, 1,
									  size_t(bytes_per_channel), files_[i]));
		}
	}

	return ret;
}

int64_t PlanarFileDevice::size() const
{
	if (isOpen()) {
		struct stat st;
		if (fstat(fileno(files_.front()), &st) == 0) {
			return int64_t(st.st_size);
		}
	}

	return 0;
}

bool PlanarFileDevice::seek(int64_t pos)
{
	bool ret = true;

	for (size_t i = 0; i < files_.size(); i++) {
		ret = (std::fseek(files_[i], pos, SEEK_SET) == 0) && ret;
	}

	return ret;
}

void PlanarFileDevice::close()
{
	for (size_t i = 0; i < files_.size(); i++) {
		std::FILE *f = files_.at(i);
		if (f) {
			std::fclose(f);
		}
	}
	files_.clear();
}

}
