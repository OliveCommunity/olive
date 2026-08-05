#pragma once
// Transitional stub for engine/render/previewautocacher.h (still Qt-based).
// Only the surface oaknode uses. M7 replaces this with the real oakrender
// boundary.
namespace olive {
class PreviewAutoCacher {
public:
	void cancel_video_tasks(bool) {}
	void cancel_audio_tasks(bool) {}
	void cancel_tasks(bool) {}
};
}
