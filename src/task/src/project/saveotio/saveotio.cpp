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

#include "saveotio.h"

#include <opentimelineio/clip.h>
#include <opentimelineio/externalReference.h>
#include <opentimelineio/gap.h>
#include <opentimelineio/serializableCollection.h>
#include <opentimelineio/serializableObject.h>
#include <opentimelineio/timeline.h>
#include <opentimelineio/transition.h>
#include <opentimelineio/version.h>

#include "node/block.h"
#include "node/folder.h"
#include "node/node.h"
#include "node/footage.h"
#include "common/videoparams.h"
#include "node/sequence.h"
#include "node/track.h"

namespace olive
{

namespace
{

std::string node_label_of(OakNodeNode *node)
{
	char buf[256];
	if (oaknode_node_get_label(node, buf, sizeof(buf)) <= 0) {
		return std::string();
	}
	return buf;
}

Rational block_in_of(OakNodeBlock *b)
{
	int n = 0, d = 1;
	oaknode_block_get_in(b, &n, &d);
	return Rational(n, d);
}

Rational block_length_of(OakNodeBlock *b)
{
	int n = 0, d = 1;
	oaknode_block_get_length(b, &n, &d);
	return Rational(n, d);
}

Rational track_length_of(OakNodeTrack *t)
{
	int n = 0, d = 1;
	oaknode_track_get_length(t, &n, &d);
	return Rational(n, d);
}

} // namespace

SaveOTIOTask::SaveOTIOTask(OakNodeProject *project,
						   const std::string &filename)
	: project_(project)
	, filename_(filename)
{
	set_title("Exporting project to OpenTimelineIO");
}

bool SaveOTIOTask::run()
{
	// Collect sequences from the root folder (non-recursive, matching the
	// original list_children_of_type behavior closely enough for OTIO)
	std::vector<OakNodeSequence *> sequences;

	OakNodeFolder *root = oaknode_project_root(project_);
	if (!root) {
		set_error("Project contains no sequences to export.");
		return false;
	}

	int child_count = oaknode_folder_child_count(root);
	for (int i = 0; i < child_count; i++) {
		OakNodeNode *child = oaknode_folder_child_at(root, i);
		if (!child) {
			continue;
		}

		char id[128];
		if (oaknode_node_get_id(child, id, sizeof(id)) <= 0) {
			continue;
		}
		if (std::string(id) == "org.olivevideoeditor.Olive.sequence") {
			sequences.push_back(
				reinterpret_cast<OakNodeSequence *>(child));
		}
	}

	if (sequences.empty()) {
		set_error("Project contains no sequences to export.");
		return false;
	}

	std::vector<OTIO::SerializableObject *> serialized;

	for (OakNodeSequence *seq : sequences) {
		auto otio_timeline = serialize_timeline(seq);

		if (otio_timeline) {
			// Append to list
			serialized.push_back(otio_timeline);
		} else {
			// Delete all existing timelines
			for (auto s : serialized) {
				s->possibly_delete();
			}

			// Error out of function
			set_error("Failed to serialize sequence \"" +
					  node_label_of(oaknode_sequence_as_node(seq)) + "\"");

			return false;
		}
	}

	OTIO::ErrorStatus es;

	if (serialized.size() == 1) {
		// Serialize timeline on its own
		auto t = serialized.front();
		t->to_json_file(filename_, &es);
		t->possibly_delete();
	} else {
		// Serialize all into a SerializableCollection
		auto collection =
			new OTIO::SerializableCollection("Sequences", serialized);
		collection->to_json_file(filename_, &es);
		collection->possibly_delete();

		// Delete all existing timelines
		for (auto s : serialized) {
			s->possibly_delete();
		}
	}

	return (es.outcome == OTIO::ErrorStatus::Outcome::OK);
}

OTIO::Timeline *SaveOTIOTask::serialize_timeline(OakNodeSequence *sequence)
{
	auto otio_timeline = new OTIO::Timeline(
		node_label_of(oaknode_sequence_as_node(sequence)));
	// Retainers clean themselves up when the final user is removed
	OTIO::Timeline::Retainer<OTIO::Timeline> *timeline_retainer =
		new OTIO::Timeline::Retainer<OTIO::Timeline>(otio_timeline);
	(void)timeline_retainer;

	double rate = 0;
	{
		int num = 0, den = 1;
		OakVideoParams vp = {};
		if (oaknode_sequence_get_video_params(sequence, 0, &vp) ==
			OAKNODE_OK) {
			oakcommon_videoparams_get_frame_rate(vp, &num, &den);
			if (den != 0) {
				rate = double(num) / den;
			}
			oakcommon_videoparams_free(&vp);
		}
	}
	if (rate != rate /* NaN */ || rate <= 0) {
		return nullptr;
	}

	OakNodeTrackList *video_list = nullptr;
	OakNodeTrackList *audio_list = nullptr;
	oaknode_sequence_get_track_list(sequence, OAKNODE_TRACK_TYPE_VIDEO,
									&video_list);
	oaknode_sequence_get_track_list(sequence, OAKNODE_TRACK_TYPE_AUDIO,
									&audio_list);

	if (!serialize_track_list(video_list, otio_timeline, rate) ||
		!serialize_track_list(audio_list, otio_timeline, rate)) {
		otio_timeline->possibly_delete();
		return nullptr;
	}

	return otio_timeline;
}

OTIO::Track *SaveOTIOTask::serialize_track(OakNodeTrack *track,
										   double sequence_rate,
										   Rational max_track_length)
{
	auto otio_track = new OTIO::Track();

	OTIO::ErrorStatus es;

	int track_type = OAKNODE_TRACK_TYPE_NONE;
	oaknode_track_get_type(track, &track_type);

	switch (track_type) {
	case OAKNODE_TRACK_TYPE_VIDEO:
		otio_track->set_kind("Video");
		break;
	case OAKNODE_TRACK_TYPE_AUDIO:
		otio_track->set_kind("Audio");
		break;
	default:
		fprintf(stderr, "Don't know OTIO track kind for native type %d\n",
				track_type);
		goto fail;
	}

	{
		int block_count = 0;
		oaknode_track_get_block_count(track, &block_count);

		for (int i = 0; i < block_count; i++) {
			OakNodeBlock *block = nullptr;
			oaknode_track_get_block_at(track, i, &block);
			if (!block) {
				continue;
			}

			OTIO::Composable *otio_block = nullptr;

			int kind = OAKNODE_BLOCK_OTHER;
			oaknode_block_get_kind(block, &kind);

			if (kind == OAKNODE_BLOCK_CLIP) {
				auto otio_clip = new OTIO::Clip(
					node_label_of(oaknode_block_as_node(block)));

				otio_clip->set_source_range(OTIO::TimeRange(
					block_in_of(block).toRationalTime(sequence_rate),
					block_length_of(block).toRationalTime(sequence_rate)));

				OakNodeFootage *media = nullptr;
				oaknode_node_find_input_footage(
					oaknode_block_as_node(block), &media);
				if (media) {
					OTIO::TimeRange available_range;
					if (track_type == OAKNODE_TRACK_TYPE_VIDEO) {
						// OTIO ExternalReference uses the source clips frame rate (or sample rate) as opposed to
						// the sequences rate
						double source_frame_rate = 0;
						double duration = 0;
						int num = 0, den = 1;
						OakVideoParams vp = {};
						if (oaknode_footage_get_video_params(media, 0,
															 &vp) ==
							OAKNODE_OK) {
							oakcommon_videoparams_get_frame_rate(vp, &num,
																 &den);
							if (den != 0) {
								source_frame_rate = double(num) / den;
							}
							int64_t dur = 0;
							oakcommon_videoparams_get_duration(vp, &dur);
							duration = double(dur);
							oakcommon_videoparams_free(&vp);
						}

						available_range = OTIO::TimeRange(
							OTIO::RationalTime(0, source_frame_rate),
							OTIO::RationalTime(duration, source_frame_rate));
					} else {
						available_range = OTIO::TimeRange(
							OTIO::RationalTime(0, 48000),
							OTIO::RationalTime(0, 48000));
					}
					char media_url[1024];
					if (oaknode_footage_filename(media, media_url,
												 sizeof(media_url)) > 0) {
						auto media_ref = new OTIO::ExternalReference(
							media_url, available_range);
						otio_clip->set_media_reference(media_ref);
					}
				}

				otio_block = otio_clip;
			} else if (kind == OAKNODE_BLOCK_GAP) {
				otio_block = new OTIO::Gap(
					OTIO::TimeRange(block_in_of(block).toRationalTime(),
									block_length_of(block).toRationalTime()),
					node_label_of(oaknode_block_as_node(block)));
			} else if (kind == OAKNODE_BLOCK_TRANSITION) {
				auto otio_transition = new OTIO::Transition(
					node_label_of(oaknode_block_as_node(block)));

				int n = 0, d = 1;
				oaknode_transition_get_in_offset(block, &n, &d);
				otio_transition->set_in_offset(
					Rational(n, d).toRationalTime());
				oaknode_transition_get_out_offset(block, &n, &d);
				otio_transition->set_out_offset(
					Rational(n, d).toRationalTime());

				otio_block = otio_transition;
			}

			if (!otio_block) {
				// We shouldn't ever get here, but catch without crashing if we ever do
				goto fail;
			}

			otio_track->append_child(otio_block, &es);

			if (es.outcome != OTIO::ErrorStatus::Outcome::OK) {
				goto fail;
			}
		}
	}

	// All OTIO tracks must have the same duration so we add a Gap to fill the remaining time
	if (otio_track->duration(&es).to_seconds() <
		max_track_length.to_double()) {
		double time_left = max_track_length.to_double() -
						   otio_track->duration(&es).to_seconds();

		OTIO::Gap *gap = new OTIO::Gap(OTIO::TimeRange(
			otio_track->duration(&es), OTIO::RationalTime(time_left, 1.0)));
		otio_track->append_child(gap, &es);

		if (es.outcome != OTIO::ErrorStatus::Outcome::OK) {
			goto fail;
		}
	}

	return otio_track;

fail:
	otio_track->possibly_delete();

	return nullptr;
}

bool SaveOTIOTask::serialize_track_list(OakNodeTrackList *list,
										OTIO::Timeline *otio_timeline,
										double sequence_rate)
{
	if (!list) {
		return true;
	}

	OTIO::ErrorStatus es;

	Rational max_track_length = RATIONAL_MIN;

	int track_count = 0;
	oaknode_tracklist_get_track_count(list, &track_count);

	for (int i = 0; i < track_count; i++) {
		OakNodeTrack *track = nullptr;
		oaknode_tracklist_get_track_at(list, i, &track);
		if (track && track_length_of(track) > max_track_length) {
			max_track_length = track_length_of(track);
		}
	}

	for (int i = 0; i < track_count; i++) {
		OakNodeTrack *track = nullptr;
		oaknode_tracklist_get_track_at(list, i, &track);
		if (!track) {
			continue;
		}

		auto otio_track = serialize_track(track, sequence_rate,
										  max_track_length);

		if (!otio_track) {
			return false;
		}

		otio_timeline->tracks()->append_child(otio_track, &es);

		if (es.outcome != OTIO::ErrorStatus::Outcome::OK) {
			otio_track->possibly_delete();
			return false;
		}
	}

	return true;
}

}
