#include <gtest/gtest.h>

#include <QVector>

#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/math/math/math.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"

namespace
{

olive::Sequence *create_sequence(olive::Project *project)
{
	auto *sequence = new olive::Sequence();
	sequence->setParent(project);
	return sequence;
}

olive::Track *create_track(olive::Project *project)
{
	auto *track = new olive::Track();
	track->setParent(project);
	return track;
}

olive::ClipBlock *create_clip(olive::Project *project,
							 const olive::core::Rational &length)
{
	auto *clip = new olive::ClipBlock();
	clip->setParent(project);
	clip->set_length_and_media_out(length);
	return clip;
}

// Mirrors what TimelineAddTrackCommand::redo() does to wire a track into a
// sequence: grow the track input array, then connect the edge
void append_track_to_list(olive::TrackList *list, olive::Track *track)
{
	list->array_append();
	olive::Node::connect_edge(track,
							 list->track_input(list->array_size() - 1));
}

} // namespace

TEST(Sequence, DefaultState)
{
	olive::Sequence sequence;

	EXPECT_EQ(sequence.name(), QStringLiteral("Sequence"));
	EXPECT_EQ(sequence.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.sequence"));
	EXPECT_TRUE(sequence.category().contains(olive::Node::k_category_project));

	// Track input ids are generated from kTrackInputFormat
	EXPECT_EQ(olive::Sequence::k_track_input_format.arg(0),
			  QStringLiteral("track_in_0"));
	EXPECT_EQ(olive::Sequence::k_track_input_format.arg(1),
			  QStringLiteral("track_in_1"));
	EXPECT_EQ(olive::Sequence::k_track_input_format.arg(2),
			  QStringLiteral("track_in_2"));

	// One track list per track type, all empty
	olive::TrackList *video = sequence.track_list(olive::Track::k_video);
	olive::TrackList *audio = sequence.track_list(olive::Track::k_audio);
	olive::TrackList *subtitle = sequence.track_list(olive::Track::k_subtitle);
	ASSERT_NE(video, nullptr);
	ASSERT_NE(audio, nullptr);
	ASSERT_NE(subtitle, nullptr);

	EXPECT_EQ(video->type(), olive::Track::k_video);
	EXPECT_EQ(audio->type(), olive::Track::k_audio);
	EXPECT_EQ(subtitle->type(), olive::Track::k_subtitle);

	EXPECT_EQ(video->track_input(), QStringLiteral("track_in_0"));
	EXPECT_EQ(audio->track_input(), QStringLiteral("track_in_1"));
	EXPECT_EQ(subtitle->track_input(), QStringLiteral("track_in_2"));

	EXPECT_EQ(video->parent(), &sequence);
	EXPECT_EQ(audio->parent(), &sequence);
	EXPECT_EQ(subtitle->parent(), &sequence);

	EXPECT_EQ(video->get_track_count(), 0);
	EXPECT_EQ(video->get_total_length(), olive::core::Rational(0));
	EXPECT_EQ(video->array_size(), 0);
	EXPECT_EQ(video->get_track_at(0), nullptr);
	EXPECT_EQ(video->get_track_at(-1), nullptr);

	EXPECT_TRUE(sequence.get_tracks().isEmpty());
	EXPECT_TRUE(sequence.get_unlocked_tracks().isEmpty());
	EXPECT_EQ(sequence.get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_video, 0)),
			  nullptr);

	// Invalid and out-of-range reference types return null instead of crashing
	EXPECT_EQ(sequence.get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_none, 0)),
			  nullptr);
	EXPECT_EQ(sequence.get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_count, 0)),
			  nullptr);

	// Length verification over empty track lists keeps everything at zero
	sequence.verify_length();
	EXPECT_EQ(sequence.get_length(), olive::core::Rational(0));
	EXPECT_EQ(sequence.get_video_length(), olive::core::Rational(0));
	EXPECT_EQ(sequence.get_audio_length(), olive::core::Rational(0));
	EXPECT_EQ(sequence.get_playhead(), olive::core::Rational(0));
}

TEST(Sequence, RetranslateSetsTrackInputNames)
{
	olive::Sequence sequence;

	sequence.retranslate();

	EXPECT_EQ(sequence.get_input_name(olive::Sequence::k_track_input_format.arg(
					  olive::Track::k_video)),
			  QStringLiteral("Video Tracks"));
	EXPECT_EQ(sequence.get_input_name(olive::Sequence::k_track_input_format.arg(
					  olive::Track::k_audio)),
			  QStringLiteral("Audio Tracks"));
	EXPECT_EQ(sequence.get_input_name(olive::Sequence::k_track_input_format.arg(
					  olive::Track::k_subtitle)),
			  QStringLiteral("Subtitle Tracks"));
}

TEST(Sequence, AddDefaultNodesCreatesVideoAndAudioTracks)
{
	olive::ColorManager::set_up_default_config();
	QVector<olive::Track *> added;
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);

	QObject::connect(sequence, &olive::Sequence::track_added,
					 [&added](olive::Track *t) { added.append(t); });

	sequence->add_default_nodes();

	olive::TrackList *video_list = sequence->track_list(olive::Track::k_video);
	olive::TrackList *audio_list = sequence->track_list(olive::Track::k_audio);
	olive::TrackList *subtitle_list =
		sequence->track_list(olive::Track::k_subtitle);

	// One video and one audio track, no subtitle tracks
	ASSERT_EQ(video_list->get_track_count(), 1);
	ASSERT_EQ(audio_list->get_track_count(), 1);
	EXPECT_EQ(subtitle_list->get_track_count(), 0);

	olive::Track *video_track = video_list->get_track_at(0);
	olive::Track *audio_track = audio_list->get_track_at(0);
	ASSERT_NE(video_track, nullptr);
	ASSERT_NE(audio_track, nullptr);

	EXPECT_EQ(video_track->type(), olive::Track::k_video);
	EXPECT_EQ(audio_track->type(), olive::Track::k_audio);
	EXPECT_EQ(video_track->sequence(), sequence);
	EXPECT_EQ(audio_track->sequence(), sequence);
	EXPECT_EQ(video_track->index(), 0);
	EXPECT_EQ(audio_track->index(), 0);

	// Both tracks were reparented into the sequence's project
	EXPECT_EQ(video_track->parent(), &project);
	EXPECT_EQ(audio_track->parent(), &project);
	EXPECT_EQ(video_list->get_parent_graph(), &project);

	// The sequence forwards TrackAdded from its track lists
	ASSERT_EQ(added.size(), 2);
	EXPECT_TRUE(added.contains(video_track));
	EXPECT_TRUE(added.contains(audio_track));

	// The flattened track cache contains both tracks
	EXPECT_EQ(sequence->get_tracks().size(), 2);
	EXPECT_TRUE(sequence->get_tracks().contains(video_track));
	EXPECT_TRUE(sequence->get_tracks().contains(audio_track));
	EXPECT_EQ(sequence->get_unlocked_tracks(), sequence->get_tracks());

	// Track lookup by reference
	EXPECT_EQ(sequence->get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_video, 0)),
			  video_track);
	EXPECT_EQ(sequence->get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_audio, 0)),
			  audio_track);
	EXPECT_EQ(sequence->get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_subtitle, 0)),
			  nullptr);

	// The default tracks are wired straight into the viewer outputs
	EXPECT_TRUE(sequence->is_input_connected(olive::ViewerOutput::k_texture_input));
	EXPECT_TRUE(sequence->is_input_connected(olive::ViewerOutput::k_samples_input));
	EXPECT_EQ(sequence->get_connected_texture_output(), video_track);
	EXPECT_EQ(sequence->get_connected_sample_output(), audio_track);
}

TEST(Sequence, TrackConnectEmitsSignalsAndSetsTrackState)
{
	olive::ColorManager::set_up_default_config();
	int list_added = 0;
	int list_changed = 0;
	int sequence_added = 0;
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);
	olive::Track *track = create_track(&project);

	olive::Track *list_last_added = nullptr;
	QObject::connect(list, &olive::TrackList::track_added,
					 [&list_added, &list_last_added](olive::Track *t) {
						 ++list_added;
						 list_last_added = t;
					 });
	QObject::connect(list, &olive::TrackList::track_list_changed,
					 [&list_changed]() { ++list_changed; });
	olive::Track *sequence_last_added = nullptr;
	QObject::connect(sequence, &olive::Sequence::track_added,
					 [&sequence_added, &sequence_last_added](olive::Track *t) {
						 ++sequence_added;
						 sequence_last_added = t;
					 });

	list->array_append();
	EXPECT_EQ(list->array_size(), 1);
	EXPECT_EQ(list->get_track_count(), 0);

	olive::Node::connect_edge(track, list->track_input(0));

	EXPECT_EQ(list_added, 1);
	EXPECT_EQ(list_last_added, track);
	EXPECT_EQ(list_changed, 1);
	EXPECT_EQ(sequence_added, 1);
	EXPECT_EQ(sequence_last_added, track);

	// The track adopts the list's type, sequence and cache index
	EXPECT_EQ(track->type(), olive::Track::k_video);
	EXPECT_EQ(track->sequence(), sequence);
	EXPECT_EQ(track->index(), 0);

	EXPECT_EQ(list->get_track_count(), 1);
	EXPECT_EQ(list->get_track_at(0), track);
	EXPECT_EQ(list->get_array_index_from_cache_index(0), 0);
	EXPECT_EQ(list->get_cache_index_from_array_index(0), 0);
	EXPECT_EQ(list->get_parent_graph(), &project);

	// TrackList::track_input() builds a NodeInput pointing at the sequence
	const olive::NodeInput input = list->track_input(0);
	EXPECT_EQ(input, olive::NodeInput(
						 sequence, olive::Sequence::k_track_input_format.arg(
										   olive::Track::k_video),
						 0));

	// The sequence-level cache tracks the new track
	EXPECT_EQ(sequence->get_tracks(), QVector<olive::Track *>({ track }));
	EXPECT_EQ(sequence->get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_video, 0)),
			  track);
}

TEST(Sequence, TrackDisconnectResetsTrackState)
{
	olive::ColorManager::set_up_default_config();
	int list_removed = 0;
	int sequence_removed = 0;
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::Track *first = create_track(&project);
	olive::Track *second = create_track(&project);
	append_track_to_list(list, first);
	append_track_to_list(list, second);
	ASSERT_EQ(list->get_track_count(), 2);

	olive::Track *list_last_removed = nullptr;
	QObject::connect(list, &olive::TrackList::track_removed,
					 [&list_removed, &list_last_removed](olive::Track *t) {
						 ++list_removed;
						 list_last_removed = t;
					 });
	QObject::connect(sequence, &olive::Sequence::track_removed,
					 [&sequence_removed](olive::Track *) {
						 ++sequence_removed;
					 });

	// While connected, track height changes are forwarded by the list
	int height_changed = 0;
	QObject::connect(list, &olive::TrackList::track_height_changed,
					 [&height_changed](olive::Track *, int) {
						 ++height_changed;
					 });
	first->set_track_height(first->get_track_height() + 1.0);
	EXPECT_EQ(height_changed, 1);

	olive::Node::disconnect_edge(first, list->track_input(0));

	EXPECT_EQ(list_removed, 1);
	EXPECT_EQ(list_last_removed, first);
	EXPECT_EQ(sequence_removed, 1);

	// The removed track is fully detached from the sequence
	EXPECT_EQ(first->sequence(), nullptr);
	EXPECT_EQ(first->type(), olive::Track::k_none);
	EXPECT_EQ(first->index(), -1);

	// Subsequent tracks shift down in the cache and get re-indexed
	EXPECT_EQ(list->get_track_count(), 1);
	EXPECT_EQ(list->get_track_at(0), second);
	EXPECT_EQ(second->index(), 0);

	// The array element itself stays; only the cache mapping moves
	EXPECT_EQ(list->array_size(), 2);
	EXPECT_EQ(list->get_cache_index_from_array_index(0), -1);
	EXPECT_EQ(list->get_cache_index_from_array_index(1), 0);
	EXPECT_EQ(list->get_array_index_from_cache_index(0), 1);

	EXPECT_EQ(sequence->get_tracks(), QVector<olive::Track *>({ second }));
	EXPECT_EQ(sequence->get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_video, 0)),
			  second);
	EXPECT_EQ(sequence->get_track_from_reference(
				  olive::Track::Reference(olive::Track::k_video, 1)),
			  nullptr);

	// Height changes on the removed track must no longer be forwarded
	first->set_track_height(first->get_track_height() + 1.0);
	EXPECT_EQ(height_changed, 1);
}

TEST(TrackList, CacheOrderFollowsArrayIndex)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::Track *first = create_track(&project);
	olive::Track *second = create_track(&project);

	list->array_append();
	list->array_append();

	// Connect the higher array element first; it takes cache index 0 for now
	olive::Node::connect_edge(second, list->track_input(1));
	EXPECT_EQ(list->get_track_count(), 1);
	EXPECT_EQ(list->get_track_at(0), second);
	EXPECT_EQ(second->index(), 0);
	EXPECT_EQ(list->get_cache_index_from_array_index(0), -1);
	EXPECT_EQ(list->get_cache_index_from_array_index(1), 0);

	// Connecting element 0 inserts ahead of it in the cache
	olive::Node::connect_edge(first, list->track_input(0));
	EXPECT_EQ(list->get_track_count(), 2);
	EXPECT_EQ(list->get_track_at(0), first);
	EXPECT_EQ(list->get_track_at(1), second);
	EXPECT_EQ(first->index(), 0);
	EXPECT_EQ(second->index(), 1);
	EXPECT_EQ(list->get_cache_index_from_array_index(0), 0);
	EXPECT_EQ(list->get_cache_index_from_array_index(1), 1);

	// Disconnecting element 0 re-indexes the remainder of the cache
	olive::Node::disconnect_edge(first, list->track_input(0));
	EXPECT_EQ(list->get_track_count(), 1);
	EXPECT_EQ(list->get_track_at(0), second);
	EXPECT_EQ(second->index(), 0);
}

TEST(TrackList, ArrayAppendAndRemoveLast)
{
	olive::Sequence sequence;
	olive::TrackList *list = sequence.track_list(olive::Track::k_audio);

	EXPECT_EQ(list->array_size(), 0);

	list->array_append();
	EXPECT_EQ(list->array_size(), 1);
	EXPECT_EQ(list->get_track_count(), 0);

	list->array_append();
	EXPECT_EQ(list->array_size(), 2);
	EXPECT_EQ(list->get_track_count(), 0);

	list->array_remove_last();
	EXPECT_EQ(list->array_size(), 1);

	list->array_remove_last();
	EXPECT_EQ(list->array_size(), 0);
}

TEST(TrackList, NonTrackAndArrayWideConnectionsAreIgnored)
{
	olive::Sequence sequence;
	olive::TrackList *list = sequence.track_list(olive::Track::k_video);

	olive::MathNode math;
	olive::Track track;

	int changed = 0;
	QObject::connect(list, &olive::TrackList::track_list_changed,
					 [&changed]() { ++changed; });

	// Nodes that are not Tracks never enter the cache
	list->track_connected(&math, 0);
	EXPECT_EQ(list->get_track_count(), 0);
	EXPECT_EQ(changed, 0);

	list->track_disconnected(&math, 0);
	EXPECT_EQ(list->get_track_count(), 0);
	EXPECT_EQ(changed, 0);

	// Element -1 means the whole array was replaced; the cache is left alone
	list->track_connected(&track, -1);
	EXPECT_EQ(list->get_track_count(), 0);
	EXPECT_EQ(changed, 0);
	EXPECT_EQ(track.sequence(), nullptr);

	list->track_disconnected(&track, -1);
	EXPECT_EQ(list->get_track_count(), 0);
	EXPECT_EQ(changed, 0);
}

TEST(Sequence, LengthFlowsFromTracksToLengthCache)
{
	olive::ColorManager::set_up_default_config();
	QVector<olive::core::Rational> lengths;
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);

	QObject::connect(sequence, &olive::ViewerOutput::length_changed,
					 [&lengths](const olive::core::Rational &r) {
						 lengths.append(r);
					 });

	// A video track with content drives the video length and total length
	olive::Track *video_track = create_track(&project);
	video_track->append_block(create_clip(&project, olive::core::Rational(5)));
	append_track_to_list(sequence->track_list(olive::Track::k_video), video_track);

	EXPECT_EQ(sequence->get_video_length(), olive::core::Rational(5));
	EXPECT_EQ(sequence->get_audio_length(), olive::core::Rational(0));
	EXPECT_EQ(sequence->get_length(), olive::core::Rational(5));

	// A longer audio track takes over the total length
	olive::Track *audio_track = create_track(&project);
	audio_track->append_block(create_clip(&project, olive::core::Rational(7)));
	append_track_to_list(sequence->track_list(olive::Track::k_audio), audio_track);

	EXPECT_EQ(sequence->get_video_length(), olive::core::Rational(5));
	EXPECT_EQ(sequence->get_audio_length(), olive::core::Rational(7));
	EXPECT_EQ(sequence->get_length(), olive::core::Rational(7));

	// Extending a connected track ripples through to the sequence length
	video_track->append_block(create_clip(&project, olive::core::Rational(5)));

	EXPECT_EQ(sequence->get_video_length(), olive::core::Rational(10));
	EXPECT_EQ(sequence->get_audio_length(), olive::core::Rational(7));
	EXPECT_EQ(sequence->get_length(), olive::core::Rational(10));

	// LengthChanged only fires when the total length actually changes
	EXPECT_EQ(lengths,
			  QVector<olive::core::Rational>({ olive::core::Rational(5),
											   olive::core::Rational(7),
											   olive::core::Rational(10) }));
}

TEST(Sequence, SubtitleTrackLengthContributesToTotalLength)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);

	olive::Track *subtitle_track = create_track(&project);
	subtitle_track->append_block(create_clip(&project, olive::core::Rational(3)));
	append_track_to_list(sequence->track_list(olive::Track::k_subtitle),
					  subtitle_track);

	EXPECT_EQ(subtitle_track->type(), olive::Track::k_subtitle);
	EXPECT_EQ(sequence->get_video_length(), olive::core::Rational(0));
	EXPECT_EQ(sequence->get_audio_length(), olive::core::Rational(0));
	EXPECT_EQ(sequence->get_length(), olive::core::Rational(3));
}

TEST(Sequence, GetUnlockedTracksOmitsLocked)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);

	olive::Track *video_a = create_track(&project);
	olive::Track *video_b = create_track(&project);
	olive::Track *audio = create_track(&project);
	append_track_to_list(sequence->track_list(olive::Track::k_video), video_a);
	append_track_to_list(sequence->track_list(olive::Track::k_video), video_b);
	append_track_to_list(sequence->track_list(olive::Track::k_audio), audio);

	// The flattened cache is ordered by track type (video, then audio)
	EXPECT_EQ(sequence->get_tracks(),
			  QVector<olive::Track *>({ video_a, video_b, audio }));
	EXPECT_EQ(sequence->get_unlocked_tracks(), sequence->get_tracks());

	video_b->set_locked(true);
	EXPECT_EQ(sequence->get_unlocked_tracks(),
			  QVector<olive::Track *>({ video_a, audio }));

	video_b->set_locked(false);
	EXPECT_EQ(sequence->get_unlocked_tracks(), sequence->get_tracks());
}

TEST(Sequence, SubtitleInvalidateEmitsSubtitlesChanged)
{
	olive::ColorManager::set_up_default_config();
	QVector<olive::core::TimeRange> received;
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);

	QObject::connect(sequence, &olive::Sequence::subtitles_changed,
					 [&received](const olive::core::TimeRange &r) {
						 received.append(r);
					 });

	// Invalidations from the subtitle track input are forwarded as a signal
	// (Sequence's override hides the base-class default arguments, so the
	// element and options must be passed explicitly)
	const olive::core::TimeRange subtitle_range(olive::core::Rational(1),
												olive::core::Rational(2));
	sequence->invalidate_cache(subtitle_range,
							  olive::Sequence::k_track_input_format.arg(
								  olive::Track::k_subtitle),
							  -1, olive::Node::InvalidateCacheOptions());

	EXPECT_EQ(received, QVector<olive::core::TimeRange>({ subtitle_range }));

	// Invalidations from other inputs do not emit the signal
	sequence->invalidate_cache(
		olive::core::TimeRange(olive::core::Rational(3),
							   olive::core::Rational(4)),
		olive::Sequence::k_track_input_format.arg(olive::Track::k_video), -1,
		olive::Node::InvalidateCacheOptions());
	sequence->invalidate_cache(
		olive::core::TimeRange(olive::core::Rational(3),
							   olive::core::Rational(4)),
		olive::ViewerOutput::k_texture_input, -1,
		olive::Node::InvalidateCacheOptions());

	EXPECT_EQ(received.size(), 1);
}

TEST(Sequence, TrackHeightChangePropagatesThroughTrackList)
{
	olive::ColorManager::set_up_default_config();
	int emissions = 0;
	int signal_height = 0;
	olive::Project project;
	project.initialize();
	olive::Sequence *sequence = create_sequence(&project);
	olive::TrackList *list = sequence->track_list(olive::Track::k_video);

	olive::Track *track = create_track(&project);
	append_track_to_list(list, track);

	olive::Track *signal_track = nullptr;
	QObject::connect(list, &olive::TrackList::track_height_changed,
					 [&emissions, &signal_track, &signal_height](
						 olive::Track *t, int h) {
						 ++emissions;
						 signal_track = t;
						 signal_height = h;
					 });

	track->set_track_height_in_pixels(96);

	EXPECT_EQ(emissions, 1);
	EXPECT_EQ(signal_track, track);
	EXPECT_EQ(signal_height, 96);
}
