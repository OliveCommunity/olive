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

#include "loadotio.h"

#include <filesystem>
#include <map>

#include <olive/core/util/rational.h>

#include <opentimelineio/clip.h>
#include <opentimelineio/externalReference.h>
#include <opentimelineio/gap.h>
#include <opentimelineio/serializableCollection.h>
#include <opentimelineio/timeline.h>
#include <opentimelineio/transition.h>
#include <opentimelineio/version.h>

#include "node/block.h"
#include "node/factory.h"
#include "node/folder.h"
#include "node/footage.h"
#include "node/node.h"
#include "node/project.h"
#include "node/sequence.h"
#include "node/track.h"
#include "timeline/edit.h"

namespace OTIO = opentimelineio::OPENTIMELINEIO_VERSION;

namespace olive
{

using core::Rational;

namespace
{

const char *k_sequence_id = "org.olivevideoeditor.Olive.sequence";
const char *k_transform_id = "org.olivevideoeditor.Olive.transform";
const char *k_volume_id = "org.olivevideoeditor.Olive.volume";

void set_own_context_position(OakNodeNode *node, double x, double y)
{
	oaknode_node_set_context_position(node, node, x, y, 0);
}

} // namespace

LoadOTIOTask::ImportConfirmFn LoadOTIOTask::confirm_callback_;

LoadOTIOTask::LoadOTIOTask(const std::string &filename)
	: ProjectLoadBaseTask(filename)
{
}

bool LoadOTIOTask::run()
{
	OTIO::ErrorStatus es;

	auto root = OTIO::SerializableObjectWithMetadata::from_json_file(
		get_filename(), &es);

	if (es.outcome != OTIO::ErrorStatus::Outcome::OK) {
		set_error("Failed to load OpenTimelineIO from file \"" +
				  get_filename() + "\" \n\nOpenTimelineIO Error:\n\n" +
				  es.full_description);
		return false;
	}

	project_ = oaknode_project_init();
	if (!project_) {
		set_error("Failed to create project");
		return false;
	}
	oaknode_project_initialize(project_);
	oaknode_project_set_modified(project_, 1);

	std::vector<OTIO::Timeline *> timelines;

	if (root->schema_name() == "SerializableCollection") {
		// This is a number of timelines
		std::vector<OTIO::SerializableObject::Retainer<OTIO::SerializableObject>>
			&root_children =
				static_cast<OTIO::SerializableCollection *>(root)->children();

		timelines.resize(root_children.size());
		for (size_t j = 0; j < root_children.size(); j++) {
			timelines[j] =
				static_cast<OTIO::Timeline *>(root_children[j].value);
		}
	} else if (root->schema_name() == "Timeline") {
		// This is a single timeline
		timelines.push_back(static_cast<OTIO::Timeline *>(root));
	} else {
		// Unknown root, we don't know what to do with this
		set_error("Unknown OpenTimelineIO root element");
		oaknode_project_free(project_);
		project_ = nullptr;
		return false;
	}

	// Keep track of imported footage
	std::map<std::string, OakNodeFootage *> imported_footage;
	std::map<OTIO::Timeline *, OakNodeSequence *> timeline_sequence_map;

	// Variables used for loading bar
	float number_of_clips = 0;
	float clips_done = 0;

	// Generate a list of sequences with the same names as the timelines.
	// Assumes each timeline has a unique name.
	int unnamed_sequence_count = 0;
	for (auto timeline : timelines) {
		OakNodeSequence *sequence = oaknode_sequence_create();
		if (!sequence) {
			continue;
		}

		std::string label;
		if (!timeline->name().empty()) {
			label = timeline->name();
		} else {
			// If the otio timeline does not provide a name, create a default one here
			unnamed_sequence_count++;
			label = "Sequence " + std::to_string(unnamed_sequence_count);
		}
		oaknode_node_set_label(oaknode_sequence_as_node(sequence),
							   label.c_str());

		// Set default params incase they aren't edited.
		oaknode_sequence_set_default_parameters(sequence);
		timeline_sequence_map.insert({ timeline, sequence });

		// Get number of clips for loading bar
		for (auto track : timeline->tracks()->children()) {
			auto otio_track = static_cast<OTIO::Track *>(track.value);
			number_of_clips += float(otio_track->children().size());
		}
	}
	if (number_of_clips <= 0) {
		number_of_clips = 1;
	}

	// Ask the user which sequences to import (facade callback; headless
	// default accepts everything)
	std::vector<std::string> sequence_names;
	sequence_names.reserve(timeline_sequence_map.size());
	for (const auto &pair : timeline_sequence_map) {
		char buf[256];
		if (oaknode_node_get_label(oaknode_sequence_as_node(pair.second),
								   buf, sizeof(buf)) > 0) {
			sequence_names.emplace_back(buf);
		} else {
			sequence_names.emplace_back();
		}
	}

	bool accepted =
		confirm_callback_ ? confirm_callback_(sequence_names) : true;

	if (!accepted) {
		// Cancel to indicate to caller that this task did not complete and to simply dispose of it
		cancel();
		for (const auto &pair : timeline_sequence_map) {
			oaknode_sequence_free(pair.second);
		}
		return true;
	}

	for (const auto &pair : timeline_sequence_map) {
		OTIO::Timeline *timeline = pair.first;
		OakNodeSequence *sequence = pair.second;
		OakNodeNode *sequence_node = oaknode_sequence_as_node(sequence);

		oaknode_project_add_node(project_, sequence_node);
		OakUndoCommand *add_seq = oaknode_command_create_folder_add_child(
			oaknode_project_root(project_), sequence_node);
		if (add_seq) {
			oakundo_command_redo_now(add_seq);
			oakundo_command_free(add_seq);
		}

		// Create a folder for this sequence's footage
		OakNodeFolder *sequence_footage =
			oaknode_folder_create(project_);
		if (sequence_footage) {
			oaknode_node_set_label(oaknode_folder_as_node(sequence_footage),
								   timeline->name().c_str());
			OakUndoCommand *add_folder =
				oaknode_command_create_folder_add_child(
					oaknode_project_root(project_),
					oaknode_folder_as_node(sequence_footage));
			if (add_folder) {
				oakundo_command_redo_now(add_folder);
				oakundo_command_free(add_folder);
			}
		}

		// Iterate through tracks
		for (auto c : timeline->tracks()->children()) {
			auto otio_track = static_cast<OTIO::Track *>(c.value);

			// Create a new track
			OakNodeTrack *track = nullptr;

			// Determine what kind of track it is
			int track_type = OAKNODE_TRACK_TYPE_NONE;
			if (otio_track->kind() == "Video") {
				track_type = OAKNODE_TRACK_TYPE_VIDEO;
			} else if (otio_track->kind() == "Audio") {
				track_type = OAKNODE_TRACK_TYPE_AUDIO;
			} else {
				fprintf(stderr, "Found unknown track type: %s\n",
						otio_track->kind().c_str());
				continue;
			}

			{
				OakNodeTrackList *track_list = nullptr;
				oaknode_sequence_get_track_list(sequence, track_type,
												&track_list);
				OakUndoCommand *add_track =
					oaktimeline_add_track_command(track_list);
				if (add_track) {
					oakundo_command_redo_now(add_track);
					oakundo_command_free(add_track);
				}

				int count = 0;
				oaknode_tracklist_get_track_count(track_list, &count);
				if (count > 0) {
					oaknode_tracklist_get_track_at(track_list, count - 1,
												   &track);
				}
			}

			if (!track) {
				continue;
			}

			// Get clips from track
			auto clip_map = otio_track->children();

			OakNodeBlock *previous_block = nullptr;
			bool prev_block_transition = false;

			for (auto otio_block_retainer : clip_map) {
				auto otio_block = otio_block_retainer.value;

				OakNodeBlock *block = nullptr;

				if (otio_block->schema_name() == "Clip") {
					block = oaknode_block_clip_create();

				} else if (otio_block->schema_name() == "Gap") {
					block = oaknode_block_gap_create();

				} else if (otio_block->schema_name() == "Transition") {
					// Todo: Look into OTIO supported transitions and add them to Olive
					block = oaknode_block_transition_create(
						OAKNODE_TRANSITION_CROSS_DISSOLVE);

				} else {
					// We don't know what this is yet, just create a gap for now so that *something* is there
					fprintf(stderr, "Found unknown block type: %s\n",
							otio_block->schema_name().c_str());
					block = oaknode_block_gap_create();
				}

				if (!block) {
					continue;
				}

				oaknode_project_add_node(project_,
										 oaknode_block_as_node(block));
				oaknode_node_set_label(oaknode_block_as_node(block),
									   otio_block->name().c_str());

				oaknode_track_append_block(track, block);

				if (otio_block->schema_name() == "Clip" ||
					otio_block->schema_name() == "Gap") {
					double start_seconds =
						static_cast<OTIO::Item *>(otio_block)
							->source_range()
							->start_time()
							.to_seconds();
					double duration_seconds =
						static_cast<OTIO::Item *>(otio_block)
							->source_range()
							->duration()
							.to_seconds();

					Rational start_time =
						Rational::from_double(start_seconds);
					Rational duration =
						Rational::from_double(duration_seconds);

					if (otio_block->schema_name() == "Clip") {
						oaknode_clip_set_media_in(
							block, start_time.numerator(),
							start_time.denominator());
					}
					oaknode_block_set_length_and_media_out(
						block, duration.numerator(),
						duration.denominator());
				}

				// If the previous block was a transition, connect the current block to it
				if (prev_block_transition) {
					oaknode_node_connect(
						oaknode_block_as_node(block),
						oaknode_block_as_node(previous_block),
						OAKNODE_TRANSITION_IN_BLOCK_INPUT);
					prev_block_transition = false;
				}

				if (otio_block->schema_name() == "Transition") {
					OTIO::Transition *otio_block_transition =
						static_cast<OTIO::Transition *>(otio_block);

					// Set how far the transition eats into the previous clip
					Rational in_offset = Rational::fromRationalTime(
						otio_block_transition->in_offset());
					Rational out_offset = Rational::fromRationalTime(
						otio_block_transition->out_offset());
					oaknode_transition_set_offsets_and_length(
						block, in_offset.numerator(), in_offset.denominator(),
						out_offset.numerator(), out_offset.denominator());

					if (previous_block) {
						oaknode_node_connect(
							oaknode_block_as_node(previous_block),
							oaknode_block_as_node(block),
							OAKNODE_TRANSITION_OUT_BLOCK_INPUT);
					}
					prev_block_transition = true;

					// Position transition in its own context
					set_own_context_position(oaknode_block_as_node(block),
											 0, 0);
				}

				if (otio_block->schema_name() == "Gap") {
					// Position gap in its own context
					set_own_context_position(oaknode_block_as_node(block),
											 0, 0);
				}

				// Update this after it's used but before any continue statements
				previous_block = block;

				if (otio_block->schema_name() == "Clip") {
					auto otio_clip = static_cast<OTIO::Clip *>(otio_block);
					if (!otio_clip->media_reference()) {
						continue;
					}
					if (otio_clip->media_reference()->schema_name() ==
						"ExternalReference") {
						// Link footage
						std::string footage_url =
							static_cast<OTIO::ExternalReference *>(
								otio_clip->media_reference())
								->target_url();

						OakNodeFootage *probed_item = nullptr;

						auto it = imported_footage.find(footage_url);
						if (it != imported_footage.end()) {
							probed_item = it->second;
						} else {
							probed_item = oaknode_footage_create(
								project_, footage_url.c_str());
							if (probed_item) {
								imported_footage.insert(
									{ footage_url, probed_item });

								std::string label =
									std::filesystem::path(footage_url)
										.filename()
										.string();
								oaknode_node_set_label(
									oaknode_footage_as_node(probed_item),
									label.c_str());

								if (sequence_footage) {
									OakUndoCommand *add_footage =
										oaknode_command_create_folder_add_child(
											sequence_footage,
											oaknode_footage_as_node(
												probed_item));
									if (add_footage) {
										oakundo_command_redo_now(add_footage);
										oakundo_command_free(add_footage);
									}
								}
							}
						}

						if (probed_item) {
							// Position clip in its own context
							set_own_context_position(
								oaknode_block_as_node(block), 0, 0);

							// Position footage in its context
							oaknode_node_set_context_position(
								oaknode_block_as_node(block),
								oaknode_footage_as_node(probed_item), -2, 0,
								0);

							if (track_type == OAKNODE_TRACK_TYPE_VIDEO) {
								OakNodeNode *transform =
									oaknode_factory_create_from_id(
										k_transform_id);
								if (transform) {
									oaknode_project_add_node(project_,
															 transform);

									oaknode_node_connect(
										oaknode_footage_as_node(
											probed_item),
										transform, "tex_in");
									oaknode_node_connect(
										transform,
										oaknode_block_as_node(block),
										"buffer_in");
									oaknode_node_set_context_position(
										oaknode_block_as_node(block),
										transform, -1, 0, 0);
								}
							} else {
								OakNodeNode *volume_node =
									oaknode_factory_create_from_id(
										k_volume_id);
								if (volume_node) {
									oaknode_project_add_node(project_,
															 volume_node);

									oaknode_node_connect(
										oaknode_footage_as_node(
											probed_item),
										volume_node, "samples_in");
									oaknode_node_connect(
										volume_node,
										oaknode_block_as_node(block),
										"buffer_in");
									oaknode_node_set_context_position(
										oaknode_block_as_node(block),
										volume_node, -1, 0, 0);
								}
							}
						}
					}
				}
				clips_done++;
				emit_progress(clips_done / number_of_clips);
			}
		}
	}

	return true;
}

}
