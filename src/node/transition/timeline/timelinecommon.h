#pragma once
#include "olive/core/util/rational.h"
namespace olive {
enum class TrackType { k_track_type_none, k_track_type_video, k_track_type_audio, k_track_type_subtitle, k_track_type_count };
class Block;
class Track;
class Timeline {
public:
	enum MovementMode { k_none, k_move, k_trim_in, k_trim_out };
	enum ThumbnailMode { k_thumbnail_off, k_thumbnail_in_out, k_thumbnail_on };
	enum WaveformMode { k_waveforms_disabled, k_waveforms_enabled };
	static bool is_a_trim_mode(MovementMode mode)
	{
		return mode == k_trim_in || mode == k_trim_out;
	}
	struct EditToInfo {
		Track *track;
		core::Rational nearest_time;
		Block *nearest_block;
	};
};
}
