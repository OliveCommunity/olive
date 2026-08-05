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

#include "playbackcache.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>

#include "diskmanager.h"
#include "filefunctions.h"
#include "output/viewer/viewer.h"
#include "project.h"
#include "project/sequence/sequence.h"

namespace olive
{

namespace
{

// QUuid::createUuid() replacement: canonical "{8-4-4-4-12}" lowercase text
// with version 4 and variant bits set (same text format QUuid::toString()
// produced, keeping project files and cache directories compatible).
std::string create_uuid_text()
{
	static std::mt19937_64 rng(std::random_device{}());

	uint8_t bytes[16];
	for (int i = 0; i < 16; i += 8) {
		uint64_t v = rng();
		for (int j = 0; j < 8; j++) {
			bytes[i + j] = uint8_t(v >> (j * 8));
		}
	}

	// Version 4, variant 1 (same as QUuid::createUuid)
	bytes[6] = (bytes[6] & 0x0F) | 0x40;
	bytes[8] = (bytes[8] & 0x3F) | 0x80;

	static const char k_hex[] = "0123456789abcdef";
	std::string out;
	out.reserve(38);
	out += '{';
	for (int i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10) {
			out += '-';
		}
		out += k_hex[bytes[i] >> 4];
		out += k_hex[bytes[i] & 0xF];
	}
	out += '}';
	return out;
}

// QFileInfo(f).lastModified().toMSecsSinceEpoch() replacement
int64_t modification_time_msecs(const std::string &path)
{
	std::error_code ec;
	auto t = std::filesystem::last_write_time(path, ec);
	if (ec) {
		return 0;
	}
	auto sys = std::chrono::time_point_cast<std::chrono::milliseconds>(
		t - std::filesystem::file_time_type::clock::now() +
		std::chrono::system_clock::now());
	return sys.time_since_epoch().count();
}

} // namespace

void PlaybackCache::invalidate(const TimeRange &r)
{
	if (r.in() == r.out()) {
		fprintf(stderr, "Tried to invalidate zero-length range\n");
		return;
	}

	validated_.remove(r);

	if (!passthroughs_.empty()) {
		TimeRangeList::util_remove(&passthroughs_, r);
	}

	InvalidateEvent(r);

	// Was `emit invalidated(r)`
	if (invalidated_callback_) {
		invalidated_callback_(r);
	}

	if (saving_enabled_) {
		save_state();
	}
}

std::string PlaybackCache::get_this_cache_directory() const
{
	return get_this_cache_directory(get_cache_directory(), get_uuid());
}

std::string
PlaybackCache::get_this_cache_directory(const std::string &cache_path,
										const std::string &cache_id)
{
	return (std::filesystem::path(cache_path) / cache_id).string();
}

void PlaybackCache::load_state()
{
	std::string cache_dir = get_this_cache_directory();
	std::string state_path = (std::filesystem::path(cache_dir) / "state").string();

	std::error_code ec;
	if (!std::filesystem::exists(state_path, ec)) {
		// No state exists, assume nothing valid
		validated_.clear();
		passthroughs_.clear();
		return;
	}

	int64_t file_time = modification_time_msecs(state_path);
	std::FILE *f = file_time > last_loaded_state_ ? std::fopen(state_path.c_str(), "rb") :
													nullptr;
	if (f) {
		BinaryStreamReader s(f);

		uint32_t version;
		s >> version;

		LoadStateEvent(s);

		switch (version) {
		case 1: {
			int32_t valid_count, pass_count;

			s >> valid_count;
			for (int32_t i = 0; i < valid_count; i++) {
				int32_t in_num, in_den, out_num, out_den;

				s >> in_num;
				s >> in_den;
				s >> out_num;
				s >> out_den;

				validated_.insert(TimeRange(Rational(in_num, in_den),
											Rational(out_num, out_den)));
			}

			s >> pass_count;
			for (int32_t i = 0; i < pass_count; i++) {
				int32_t in_num, in_den, out_num, out_den;

				s >> in_num;
				s >> in_den;
				s >> out_num;
				s >> out_den;

				Passthrough p = TimeRange(Rational(in_num, in_den),
										  Rational(out_num, out_den));
				p.cache = s.read_uuid_text();
				passthroughs_.push_back(p);
			}

			break;
		}
		}

		std::fclose(f);

		last_loaded_state_ = file_time;
	}
}

void PlaybackCache::save_state()
{
	if (!DiskManager::instance()) {
		return;
	}

	std::string cache_dir = get_this_cache_directory();
	std::string state_path = (std::filesystem::path(cache_dir) / "state").string();
	if (validated_.isEmpty() && passthroughs_.empty()) {
		std::error_code ec;
		std::filesystem::remove(state_path, ec);
	} else {
		if (FileFunctions::directory_is_valid(cache_dir)) {
			std::FILE *f = std::fopen(state_path.c_str(), "wb");
			if (f) {
				BinaryStreamWriter s(f);

				uint32_t version = 1;
				s << version;

				SaveStateEvent(s);

				// Using "int" for backwards compatibility with when we used QVector, could potentially overflow
				s << int32_t(validated_.size());

				for (const TimeRange &r : validated_) {
					s << int32_t(r.in().numerator());
					s << int32_t(r.in().denominator());
					s << int32_t(r.out().numerator());
					s << int32_t(r.out().denominator());
				}

				// Using "int" for backwards compatibility with when we used QVector, could potentially overflow
				s << int32_t(passthroughs_.size());

				for (const Passthrough &p : passthroughs_) {
					s << int32_t(p.in().numerator());
					s << int32_t(p.in().denominator());
					s << int32_t(p.out().numerator());
					s << int32_t(p.out().denominator());
					s.write_uuid_text(p.cache);
				}

				std::fclose(f);

				last_loaded_state_ = modification_time_msecs(state_path);
			}
		}
	}
}

void PlaybackCache::set_passthrough(PlaybackCache *cache)
{
	for (const TimeRange &r : cache->get_validated_ranges()) {
		Passthrough p = r;
		p.cache = cache->get_uuid();
		passthroughs_.push_back(p);
	}

	passthroughs_.insert(passthroughs_.end(), cache->get_passthroughs().begin(),
						 cache->get_passthroughs().end());

	if (saving_enabled_) {
		save_state();
	}
}

void PlaybackCache::invalidate_all()
{
	invalidate(TimeRange(0, RATIONAL_MAX));
}

void PlaybackCache::request(ViewerOutput *context, const TimeRange &r)
{
	request_context_ = context;
	requested_.insert(r);

	if (requested_callback_) {
		requested_callback_(request_context_, r);
	}
}

void PlaybackCache::validate(const TimeRange &r, bool signal)
{
	validated_.insert(r);

	// Was `emit validated(r)` (suppressed when signal == false)
	if (signal && validated_callback_) {
		validated_callback_(r);
	}

	if (saving_enabled_) {
		save_state();
	}
}

void PlaybackCache::InvalidateEvent(const TimeRange &)
{
}

Project *PlaybackCache::get_project() const
{
	return Project::get_project_from_object(parent_);
}

PlaybackCache::PlaybackCache(Node *parent)
	: saving_enabled_(true)
	, last_loaded_state_(0)
	, parent_(parent)
{
	uuid_ = create_uuid_text();
}

void PlaybackCache::set_uuid(const std::string &u)
{
	uuid_ = u;

	load_state();
}

TimeRangeList PlaybackCache::get_invalidated_ranges(TimeRange intersecting) const
{
	TimeRangeList invalidated;

	// Prevent TimeRange from being below 0, some other behavior in Olive relies on this behavior
	// and it seemed reasonable to have safety code in here
	intersecting.set_out(std::max(Rational(0), intersecting.out()));
	intersecting.set_in(std::max(Rational(0), intersecting.in()));

	invalidated.insert(intersecting);

	for (const TimeRange &range : validated_) {
		invalidated.remove(range);
	}

	for (const TimeRange &range : passthroughs_) {
		invalidated.remove(range);
	}

	return invalidated;
}

bool PlaybackCache::has_invalidated_ranges(const TimeRange &intersecting) const
{
	return !validated_.contains(intersecting);
}

std::string PlaybackCache::get_cache_directory() const
{
	Project *project = get_project();

	if (project) {
		return project->cache_path();
	} else {
		return DiskManager::instance()->get_default_cache_path();
	}
}

}
