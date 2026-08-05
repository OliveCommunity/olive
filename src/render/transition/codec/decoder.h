#pragma once
// Transitional: engine/codec/decoder.h de-Qt target form (oakcodec contract,
// M5). Covers the Decoder/DecoderCache-facing surface the render core uses:
// stream identification, open/close, video/audio retrieval and access-time
// bookkeeping. CodecStream gains operator< because de-Qt RenderCache is
// std::map-based (QHash used qHash before).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "loopmode.h"
#include "rendermodes.h"
#include "cancelatom.h"
#include "codec/frame.h"
#include "project/footage/footagedescription.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"

namespace olive {

using core::AudioParams;
using core::Rational;
using core::SampleBuffer;
using core::TimeRange;

class Block;
class Renderer;
class Texture;
using TexturePtr = std::shared_ptr<Texture>;

class Decoder;
using DecoderPtr = std::shared_ptr<Decoder>;

class Decoder {
public:
	virtual ~Decoder() = default;

	class CodecStream {
	public:
		CodecStream()
			: stream_(-1)
			, block_(nullptr)
		{
		}

		CodecStream(const std::string &filename, int stream, Block *block)
			: filename_(filename)
			, stream_(stream)
			, block_(block)
		{
		}

		bool is_valid() const { return !filename_.empty() && stream_ >= 0; }

		void reset() { *this = CodecStream(); }

		bool operator==(const CodecStream &rhs) const
		{
			return filename_ == rhs.filename_ && stream_ == rhs.stream_;
		}

		bool operator<(const CodecStream &rhs) const
		{
			if (filename_ != rhs.filename_) {
				return filename_ < rhs.filename_;
			}
			return stream_ < rhs.stream_;
		}

		const std::string &filename() const { return filename_; }
		int stream() const { return stream_; }
		Block *block() const { return block_; }

	private:
		std::string filename_;
		int stream_;
		Block *block_;
	};

	bool open(const CodecStream &) { return false; }
	void close() {}

	// Same value as the engine original (decoder.cpp): RATIONAL_MIN.
	// inline because this stub is header-only.
	inline static const Rational k_any_timecode = RATIONAL_MIN;

	struct RetrieveVideoParams {
		Renderer *renderer = nullptr;
		Rational time;
		int divider = 1;
		PixelFormat maximum_format = PixelFormat::invalid;
		CancelAtom *cancelled = nullptr;
		VideoParams::ColorRange force_range = VideoParams::k_color_range_default;
		VideoParams::Interlacing src_interlacing = VideoParams::k_interlace_none;
	};

	TexturePtr retrieve_video(const RetrieveVideoParams &) { return nullptr; }
	FramePtr retrieve_video_frame(const RetrieveVideoParams &) { return nullptr; }

	enum RetrieveAudioStatus {
		k_invalid = -1,
		k_ok,
		k_waiting_for_conform,
		k_unknown_error
	};

	RetrieveAudioStatus retrieve_audio(SampleBuffer &, const TimeRange &,
									   const AudioParams &,
									   const std::string &, LoopMode,
									   RenderMode::Mode)
	{
		return k_invalid;
	}

	int64_t get_last_accessed_time() { return 0; }
	void increment_access_time(int64_t) {}

	static DecoderPtr create_from_id(const std::string &) { return nullptr; }

	// oaknode 侧契约（footage.cpp 探针路径）
	static std::vector<DecoderPtr> receive_list_of_all_decoders() { return {}; }

	FootageDescription probe(const std::string &, CancelAtom *)
	{
		return FootageDescription();
	}

	static std::string
	transform_image_sequence_file_name(const std::string &filename,
									   int64_t frame_number)
	{
		(void) frame_number;
		return filename;
	}
};

}
