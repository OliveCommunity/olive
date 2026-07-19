#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/globals.h"
#include "node/input/multicam/multicamnode.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project.h"
#include "node/project/footage/footagedescription.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "render/diskmanager.h"

namespace
{

// Project save/load and cache paths go through the DiskManager singleton
void ensure_disk_manager(bool *created)
{
	*created = (olive::DiskManager::instance() == nullptr);
	if (*created) {
		olive::DiskManager::create_instance();
	}
}

void release_disk_manager(bool created)
{
	if (created) {
		olive::DiskManager::destroy_instance();
	}
}

} // namespace

TEST(MultiCamNode, DefaultState)
{
	olive::MultiCamNode node;

	EXPECT_EQ(node.name(), QStringLiteral("Multi-Cam"));
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.multicam"));
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_timeline));

	// The "arraystart" property only hints the UI; the sources array itself
	// starts empty
	EXPECT_EQ(node.get_source_count(), 0);
	EXPECT_EQ(node.get_current_source(), 0);

	// Sequence type selector stays hidden until a sequence is connected
	EXPECT_TRUE(node.get_input_flags(olive::MultiCamNode::k_sequence_type_input) &
				olive::k_input_flag_hidden);
}

TEST(MultiCamNode, ActiveElementsSelectCurrentSource)
{
	olive::MultiCamNode node;
	node.input_array_resize(olive::MultiCamNode::k_sources_input, 3);
	ASSERT_EQ(node.get_source_count(), 3);

	node.set_standard_value(olive::MultiCamNode::k_current_input, 1);

	olive::Node::ActiveElements active = node.get_active_elements_at_time(
		olive::MultiCamNode::k_sources_input, olive::TimeRange());
	EXPECT_EQ(active.mode(), olive::Node::ActiveElements::k_specified);
	ASSERT_EQ(active.elements().size(), 1);
	EXPECT_EQ(active.elements().front(), 1);

	// Any other input defers to the base implementation
	olive::Node::ActiveElements all = node.get_active_elements_at_time(
		olive::MultiCamNode::k_current_input, olive::TimeRange());
	EXPECT_EQ(all.mode(), olive::Node::ActiveElements::k_all_elements);
}

TEST(MultiCamNode, ActiveElementsOutOfRangeAreEmpty)
{
	olive::MultiCamNode node;
	node.input_array_resize(olive::MultiCamNode::k_sources_input, 3);

	node.set_standard_value(olive::MultiCamNode::k_current_input, 5);
	EXPECT_EQ(node.get_active_elements_at_time(olive::MultiCamNode::k_sources_input,
										   olive::TimeRange())
				  .mode(),
			  olive::Node::ActiveElements::k_no_elements);

	node.set_standard_value(olive::MultiCamNode::k_current_input, -1);
	EXPECT_EQ(node.get_active_elements_at_time(olive::MultiCamNode::k_sources_input,
										   olive::TimeRange())
				  .mode(),
			  olive::Node::ActiveElements::k_no_elements);
}

TEST(MultiCamNode, RowsAndColumnsGrowToFitSources)
{
	const struct {
		int sources;
		int rows;
		int cols;
	} k_cases[] = { { 1, 1, 1 }, { 2, 1, 2 }, { 3, 2, 2 }, { 4, 2, 2 },
				   { 5, 2, 3 }, { 6, 2, 3 }, { 9, 3, 3 }, { 12, 3, 4 } };

	for (const auto &c : k_cases) {
		int rows = 0, cols = 0;
		olive::MultiCamNode::get_rows_and_columns(c.sources, &rows, &cols);
		EXPECT_EQ(rows, c.rows) << "sources=" << c.sources;
		EXPECT_EQ(cols, c.cols) << "sources=" << c.sources;
	}

	// The grid always fits all sources and stays as square as possible
	for (int s = 1; s <= 16; s++) {
		int rows = 0, cols = 0;
		olive::MultiCamNode::get_rows_and_columns(s, &rows, &cols);
		EXPECT_GE(rows * cols, s);
		EXPECT_LE(rows, cols);
	}
}

TEST(MultiCamNode, RowColumnIndexRoundTrip)
{
	int row = -1, col = -1;
	olive::MultiCamNode::index_to_row_cols(5, 2, 3, &row, &col);
	EXPECT_EQ(row, 1);
	EXPECT_EQ(col, 2);
	EXPECT_EQ(olive::MultiCamNode::rows_cols_to_index(1, 2, 2, 3), 5);

	const int k_rows = 3;
	const int k_cols = 4;
	for (int i = 0; i < k_rows * k_cols; i++) {
		olive::MultiCamNode::index_to_row_cols(i, k_rows, k_cols, &row, &col);
		EXPECT_EQ(olive::MultiCamNode::rows_cols_to_index(row, col, k_rows, k_cols),
				  i);
	}
}

TEST(MultiCamNode, RetranslateSetsInputNamesAndComboStrings)
{
	olive::MultiCamNode node;
	node.retranslate();

	EXPECT_EQ(node.get_input_name(olive::MultiCamNode::k_current_input),
			  QStringLiteral("Current"));
	EXPECT_EQ(node.get_input_name(olive::MultiCamNode::k_sources_input),
			  QStringLiteral("Sources"));
	EXPECT_EQ(node.get_input_name(olive::MultiCamNode::k_sequence_input),
			  QStringLiteral("Sequence"));
	EXPECT_EQ(node.get_input_name(olive::MultiCamNode::k_sequence_type_input),
			  QStringLiteral("Sequence Type"));

	EXPECT_EQ(
		node.get_combo_box_strings(olive::MultiCamNode::k_sequence_type_input),
		(QStringList{ QStringLiteral("Video"), QStringLiteral("Audio") }));

	// No sources yet, so no labels
	EXPECT_TRUE(node.get_combo_box_strings(olive::MultiCamNode::k_current_input)
					.isEmpty());
}

TEST(MultiCamNode, IgnoreInputsForRenderingSkipsSequenceInput)
{
	olive::MultiCamNode node;

	EXPECT_TRUE(node.ignore_inputs_for_rendering().contains(
		olive::MultiCamNode::k_sequence_input));
	EXPECT_FALSE(node.ignore_inputs_for_rendering().contains(
		olive::MultiCamNode::k_sources_input));
}

TEST(MultiCamNode, ValuePushesFirstSourceArrayElement)
{
	olive::MultiCamNode node;

	olive::NodeValueArray sources;
	sources[0] = olive::NodeValue(olive::NodeValue::k_text,
								  QStringLiteral("cam A"), &node);
	sources[1] = olive::NodeValue(olive::NodeValue::k_text,
								  QStringLiteral("cam B"), &node);

	olive::NodeValueRow row;
	row.insert(olive::MultiCamNode::k_sources_input,
			   olive::NodeValue(olive::NodeValue::k_none,
								QVariant::fromValue(sources), &node, true));

	olive::NodeValueTable table;
	node.value(row, olive::NodeGlobals(), &table);

	// Value() forwards the first array element; the traverser filters the
	// array down to the active element before Value() is ever called
	ASSERT_EQ(table.count(), 1);
	EXPECT_EQ(table.at(0).to_string(), QStringLiteral("cam A"));
}

TEST(MultiCamNode, ValueWithoutSourcesLeavesTableEmpty)
{
	olive::MultiCamNode node;

	olive::NodeValueTable table;
	node.value(olive::NodeValueRow(), olive::NodeGlobals(), &table);

	EXPECT_TRUE(table.isEmpty());
}

TEST(MultiCamNode, ConnectedSequenceProvidesTrackSources)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *sequence = new olive::Sequence();
	sequence->setParent(&project);
	auto *node = new olive::MultiCamNode();
	node->setParent(&project);
	node->set_sequence_type(olive::Track::k_video);

	olive::Node::connect_edge(
		sequence, olive::NodeInput(node, olive::MultiCamNode::k_sequence_input));

	// Connecting a sequence exposes the type selector
	EXPECT_FALSE(node->get_input_flags(olive::MultiCamNode::k_sequence_type_input) &
				 olive::k_input_flag_hidden);

	// The sequence has no tracks yet
	EXPECT_EQ(node->get_source_count(), 0);

	olive::TrackList *video_list = sequence->track_list(olive::Track::k_video);

	video_list->array_append();
	auto *track_a = new olive::Track();
	track_a->setParent(&project);
	olive::Node::connect_edge(track_a, video_list->track_input(0));

	video_list->array_append();
	auto *track_b = new olive::Track();
	track_b->setParent(&project);
	olive::Node::connect_edge(track_b, video_list->track_input(1));

	// Sources are now pulled from the sequence's track list
	ASSERT_EQ(node->get_source_count(), 2);
	EXPECT_EQ(node->get_connected_render_output(olive::MultiCamNode::k_sources_input,
											 0),
			  track_a);
	EXPECT_EQ(node->get_connected_render_output(olive::MultiCamNode::k_sources_input,
											 1),
			  track_b);
	EXPECT_TRUE(
		node->is_input_connected_for_render(olive::MultiCamNode::k_sources_input, 0));
	EXPECT_TRUE(
		node->is_input_connected_for_render(olive::MultiCamNode::k_sources_input, 1));

	// Past the end of the track list the overrides defer to the base class
	EXPECT_FALSE(
		node->is_input_connected_for_render(olive::MultiCamNode::k_sources_input, 2));
	EXPECT_EQ(node->get_connected_render_output(olive::MultiCamNode::k_sources_input,
											 2),
			  nullptr);

	node->set_standard_value(olive::MultiCamNode::k_current_input, 1);
	olive::Node::ActiveElements active = node->get_active_elements_at_time(
		olive::MultiCamNode::k_sources_input, olive::TimeRange());
	EXPECT_EQ(active.mode(), olive::Node::ActiveElements::k_specified);
	ASSERT_EQ(active.elements().size(), 1);
	EXPECT_EQ(active.elements().front(), 1);

	// Retranslate names each angle after its track
	node->retranslate();
	EXPECT_EQ(node->get_combo_box_strings(olive::MultiCamNode::k_current_input),
			  (QStringList{ QStringLiteral("1: Video Track 0"),
							QStringLiteral("2: Video Track 1") }));

	// Disconnecting hides the type selector and falls back to the sources array
	olive::Node::disconnect_edge(
		sequence, olive::NodeInput(node, olive::MultiCamNode::k_sequence_input));
	EXPECT_TRUE(node->get_input_flags(olive::MultiCamNode::k_sequence_type_input) &
				olive::k_input_flag_hidden);
	// With nothing appended to the sources array, it falls back to zero
	EXPECT_EQ(node->get_source_count(), 0);
}

TEST(MultiCamNode, SequenceTypeSelectsTrackList)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *sequence = new olive::Sequence();
	sequence->setParent(&project);
	auto *node = new olive::MultiCamNode();
	node->setParent(&project);

	olive::Node::connect_edge(
		sequence, olive::NodeInput(node, olive::MultiCamNode::k_sequence_input));

	node->set_sequence_type(olive::Track::k_audio);
	EXPECT_EQ(node->get_source_count(), 0);

	olive::TrackList *audio_list = sequence->track_list(olive::Track::k_audio);
	audio_list->array_append();
	auto *track = new olive::Track();
	track->setParent(&project);
	olive::Node::connect_edge(track, audio_list->track_input(0));

	EXPECT_EQ(node->get_source_count(), 1);
	EXPECT_EQ(node->get_connected_render_output(olive::MultiCamNode::k_sources_input,
											 0),
			  track);

	// Switching the type swaps which track list feeds the sources
	node->set_sequence_type(olive::Track::k_video);
	EXPECT_EQ(node->get_source_count(), 0);
}

TEST(FootageDescription, DefaultStateIsInvalid)
{
	olive::FootageDescription desc;

	EXPECT_TRUE(desc.decoder().isEmpty());
	EXPECT_EQ(desc.get_stream_count(), 0);
	EXPECT_TRUE(desc.get_video_streams().isEmpty());
	EXPECT_TRUE(desc.get_audio_streams().isEmpty());
	EXPECT_TRUE(desc.get_subtitle_streams().isEmpty());
	EXPECT_FALSE(desc.has_source_start_time());
	EXPECT_FALSE(desc.is_valid());
}

TEST(FootageDescription, ValidityRequiresDecoderAndStream)
{
	olive::VideoParams video(640, 480, olive::Rational(1, 24),
							 olive::core::PixelFormat::u8, 4);
	video.set_stream_index(0);

	// A stream without a decoder name is not enough
	olive::FootageDescription no_decoder;
	no_decoder.add_video_stream(video);
	EXPECT_FALSE(no_decoder.is_valid());

	// A decoder without any stream is not enough either
	olive::FootageDescription desc(QStringLiteral("fakedecoder"));
	EXPECT_FALSE(desc.is_valid());

	desc.add_video_stream(video);
	EXPECT_TRUE(desc.is_valid());
}

TEST(FootageDescription, StreamTypeLookup)
{
	olive::FootageDescription desc(QStringLiteral("fakedecoder"));

	olive::VideoParams video(1920, 1080, olive::Rational(1, 24),
							 olive::core::PixelFormat::u8, 4);
	video.set_stream_index(0);
	desc.add_video_stream(video);

	olive::core::AudioParams audio(48000, olive::core::k_channel_layout_stereo,
								   olive::core::SampleFormat::f32_p);
	audio.set_stream_index(1);
	desc.add_audio_stream(audio);

	olive::SubtitleParams subs;
	subs.set_stream_index(2);
	subs.push_back(olive::Subtitle(olive::TimeRange(olive::Rational(0),
													olive::Rational(3)),
								   QStringLiteral("subtitle text")));
	desc.add_subtitle_stream(subs);

	desc.set_stream_count(3);

	EXPECT_EQ(desc.decoder(), QStringLiteral("fakedecoder"));
	EXPECT_EQ(desc.get_stream_count(), 3);

	EXPECT_TRUE(desc.stream_is_video(0));
	EXPECT_FALSE(desc.stream_is_video(1));
	EXPECT_TRUE(desc.stream_is_audio(1));
	EXPECT_FALSE(desc.stream_is_audio(2));
	EXPECT_TRUE(desc.stream_is_subtitle(2));
	EXPECT_FALSE(desc.stream_is_subtitle(0));

	EXPECT_TRUE(desc.has_stream_index(0));
	EXPECT_TRUE(desc.has_stream_index(1));
	EXPECT_TRUE(desc.has_stream_index(2));
	EXPECT_FALSE(desc.has_stream_index(3));

	EXPECT_EQ(desc.get_type_of_stream(0), olive::Track::k_video);
	EXPECT_EQ(desc.get_type_of_stream(1), olive::Track::k_audio);
	EXPECT_EQ(desc.get_type_of_stream(2), olive::Track::k_subtitle);
	EXPECT_EQ(desc.get_type_of_stream(99), olive::Track::k_none);

	ASSERT_EQ(desc.get_video_streams().size(), 1);
	ASSERT_EQ(desc.get_audio_streams().size(), 1);
	ASSERT_EQ(desc.get_subtitle_streams().size(), 1);
}

TEST(FootageDescription, SaveLoadRoundTrip)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path =
		QDir(dir.path()).filePath(QStringLiteral("streamcache.xml"));

	olive::FootageDescription desc(QStringLiteral("fakedecoder"));

	olive::VideoParams video(1920, 1080, olive::Rational(1, 24),
							 olive::core::PixelFormat::u8, 4);
	video.set_stream_index(0);
	video.set_duration(48);
	desc.add_video_stream(video);

	olive::core::AudioParams audio(48000, olive::core::k_channel_layout_stereo,
								   olive::core::SampleFormat::f32_p);
	audio.set_stream_index(1);
	audio.set_duration(96000);
	desc.add_audio_stream(audio);

	olive::SubtitleParams subs;
	subs.set_stream_index(2);
	subs.push_back(olive::Subtitle(olive::TimeRange(olive::Rational(0),
													olive::Rational(3)),
								   QStringLiteral("subtitle text")));
	desc.add_subtitle_stream(subs);

	desc.set_stream_count(3);

	ASSERT_TRUE(desc.save(path));

	olive::FootageDescription loaded;
	ASSERT_TRUE(loaded.load(path));

	EXPECT_TRUE(loaded.is_valid());
	EXPECT_EQ(loaded.decoder(), QStringLiteral("fakedecoder"));
	EXPECT_EQ(loaded.get_stream_count(), 3);

	ASSERT_EQ(loaded.get_video_streams().size(), 1);
	const olive::VideoParams &loaded_video = loaded.get_video_streams().first();
	EXPECT_EQ(loaded_video.width(), 1920);
	EXPECT_EQ(loaded_video.height(), 1080);
	EXPECT_EQ(loaded_video.stream_index(), 0);
	EXPECT_EQ(loaded_video.duration(), video.duration());
	EXPECT_EQ(loaded_video.time_base(), video.time_base());

	ASSERT_EQ(loaded.get_audio_streams().size(), 1);
	const olive::core::AudioParams &loaded_audio =
		loaded.get_audio_streams().first();
	EXPECT_EQ(loaded_audio.sample_rate(), 48000);
	EXPECT_EQ(loaded_audio.channel_layout(), olive::core::k_channel_layout_stereo);
	EXPECT_EQ(loaded_audio.stream_index(), 1);
	EXPECT_EQ(loaded_audio.duration(), audio.duration());

	ASSERT_EQ(loaded.get_subtitle_streams().size(), 1);
	const olive::SubtitleParams &loaded_subs =
		loaded.get_subtitle_streams().first();
	EXPECT_EQ(loaded_subs.stream_index(), 2);
	ASSERT_EQ(loaded_subs.size(), 1);
	EXPECT_EQ(loaded_subs.front().text(), QStringLiteral("subtitle text"));
	EXPECT_EQ(loaded_subs.front().time().out(), olive::Rational(3));
}

TEST(FootageDescription, LoadMissingFileFailsAndResetsState)
{
	olive::FootageDescription desc(QStringLiteral("stale"));
	olive::VideoParams video(640, 480, olive::Rational(1, 24),
							 olive::core::PixelFormat::u8, 4);
	video.set_stream_index(0);
	desc.add_video_stream(video);
	ASSERT_TRUE(desc.is_valid());

	EXPECT_FALSE(
		desc.load(QStringLiteral("/definitely/nonexistent/cache.xml")));

	// A failed load must not leave stale streams behind
	EXPECT_TRUE(desc.decoder().isEmpty());
	EXPECT_TRUE(desc.get_video_streams().isEmpty());
	EXPECT_FALSE(desc.is_valid());
}

TEST(FootageDescription, LoadRejectsMismatchedVersion)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path =
		QDir(dir.path()).filePath(QStringLiteral("legacy-cache.xml"));

	// No version attribute: treated as the original unversioned cache format,
	// which is always discarded so the footage can be re-probed
	QFile file(path);
	ASSERT_TRUE(file.open(QFile::WriteOnly));
	file.write("<streamcache><decoder>fakedecoder</decoder></streamcache>");
	file.close();

	olive::FootageDescription desc(QStringLiteral("stale"));
	olive::VideoParams video(640, 480, olive::Rational(1, 24),
							 olive::core::PixelFormat::u8, 4);
	video.set_stream_index(0);
	desc.add_video_stream(video);

	EXPECT_FALSE(desc.load(path));
	EXPECT_TRUE(desc.decoder().isEmpty());
	EXPECT_FALSE(desc.is_valid());
}

TEST(ProjectSerializer, FileRoundTripPreservesMultiCamNode)
{
	olive::ColorManager::set_up_default_config();
	bool created_disk_manager = false;
	ensure_disk_manager(&created_disk_manager);
	olive::NodeFactory::initialize();
	olive::ProjectSerializer::initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString filename =
		QDir(dir.path()).filePath(QStringLiteral("multicam.ove"));

	olive::Project project;
	project.initialize();
	auto *node = new olive::MultiCamNode();
	node->set_label(QStringLiteral("Angles"));
	node->set_standard_value(olive::MultiCamNode::k_current_input, 2);
	node->setParent(&project);

	olive::ProjectSerializer::Result save_result =
		olive::ProjectSerializer::save(olive::ProjectSerializer::SaveData(
										   olive::ProjectSerializer::k_project,
										   &project, filename),
									   false);
	ASSERT_EQ(save_result.code(), olive::ProjectSerializer::k_success);
	ASSERT_TRUE(QFile::exists(filename));

	// An uncompressed project is plain XML
	QFile raw(filename);
	ASSERT_TRUE(raw.open(QFile::ReadOnly));
	EXPECT_TRUE(raw.read(5).startsWith("<?xml"));
	raw.close();

	olive::Project loaded_project;
	olive::ProjectSerializer::Result load_result =
		olive::ProjectSerializer::load(&loaded_project, filename,
									   olive::ProjectSerializer::k_project);
	ASSERT_EQ(load_result.code(), olive::ProjectSerializer::k_success);

	olive::MultiCamNode *loaded_node = nullptr;
	foreach (olive::Node *n, loaded_project.nodes()) {
		if ((loaded_node = dynamic_cast<olive::MultiCamNode *>(n))) {
			break;
		}
	}
	ASSERT_NE(loaded_node, nullptr);
	EXPECT_EQ(loaded_node->get_label(), QStringLiteral("Angles"));
	EXPECT_EQ(loaded_node->get_current_source(), 2);

	olive::ProjectSerializer::destroy();
	olive::NodeFactory::destroy();
	release_disk_manager(created_disk_manager);
}

TEST(ProjectSerializer, CompressedFileRoundTrip)
{
	olive::ColorManager::set_up_default_config();
	bool created_disk_manager = false;
	ensure_disk_manager(&created_disk_manager);
	olive::NodeFactory::initialize();
	olive::ProjectSerializer::initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString filename =
		QDir(dir.path()).filePath(QStringLiteral("compressed.ove"));

	olive::Project project;
	project.initialize();
	auto *node = new olive::MultiCamNode();
	node->set_label(QStringLiteral("Compressed"));
	node->setParent(&project);

	olive::ProjectSerializer::Result save_result =
		olive::ProjectSerializer::save(olive::ProjectSerializer::SaveData(
										   olive::ProjectSerializer::k_project,
										   &project, filename),
									   true);
	ASSERT_EQ(save_result.code(), olive::ProjectSerializer::k_success);

	// Compressed projects are marked with the OVEC signature
	QFile raw(filename);
	ASSERT_TRUE(raw.open(QFile::ReadOnly));
	EXPECT_EQ(raw.read(4), QByteArray("OVEC", 4));
	raw.close();

	olive::Project loaded_project;
	olive::ProjectSerializer::Result load_result =
		olive::ProjectSerializer::load(&loaded_project, filename,
									   olive::ProjectSerializer::k_project);
	ASSERT_EQ(load_result.code(), olive::ProjectSerializer::k_success);

	bool found = false;
	foreach (olive::Node *n, loaded_project.nodes()) {
		if (dynamic_cast<olive::MultiCamNode *>(n)) {
			found = true;
			EXPECT_EQ(n->get_label(), QStringLiteral("Compressed"));
			break;
		}
	}
	EXPECT_TRUE(found);

	olive::ProjectSerializer::destroy();
	olive::NodeFactory::destroy();
	release_disk_manager(created_disk_manager);
}

TEST(ProjectSerializer, LoadNonexistentFileFails)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		&project, QStringLiteral("/definitely/nonexistent/project.ove"),
		olive::ProjectSerializer::k_project);

	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_file_error);
	EXPECT_FALSE(result.get_details().isEmpty());
}

TEST(ProjectSerializer, LoadGarbageXmlReportsUnknownVersion)
{
	olive::ColorManager::set_up_default_config();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString filename =
		QDir(dir.path()).filePath(QStringLiteral("garbage.ove"));
	QFile file(filename);
	ASSERT_TRUE(file.open(QFile::WriteOnly));
	file.write("this is not a project file");
	file.close();

	olive::Project project;
	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		&project, filename, olive::ProjectSerializer::k_project);

	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_unknown_version);
}

TEST(ProjectSerializer, LoadOlderVersionReportsTooOld)
{
	olive::ProjectSerializer::initialize();

	// 190219 predates every registered serializer
	QXmlStreamReader reader(
		QStringLiteral("<olive version=\"190219\"><foo/></olive>"));
	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		nullptr, &reader, olive::ProjectSerializer::k_only_nodes);

	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_project_too_old);

	olive::ProjectSerializer::destroy();
}

TEST(ProjectSerializer, LoadNewerVersionReportsTooNew)
{
	olive::ProjectSerializer::initialize();

	QXmlStreamReader reader(
		QStringLiteral("<olive version=\"999999\"><foo/></olive>"));
	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		nullptr, &reader, olive::ProjectSerializer::k_only_nodes);

	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_project_too_new);

	olive::ProjectSerializer::destroy();
}

TEST(ProjectSerializer, LoadWithoutVersionReportsUnknown)
{
	// A project element without a version attribute
	QXmlStreamReader no_version(QStringLiteral("<olive><foo/></olive>"));
	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		nullptr, &no_version, olive::ProjectSerializer::k_only_nodes);
	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_unknown_version);

	// No recognizable root element at all
	QXmlStreamReader wrong_root(
		QStringLiteral("<unrelated><foo/></unrelated>"));
	result = olive::ProjectSerializer::load(
		nullptr, &wrong_root, olive::ProjectSerializer::k_only_nodes);
	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_unknown_version);
}

TEST(ProjectSerializer, CheckCompressedIDDetectsSignature)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	const QString compressed =
		QDir(dir.path()).filePath(QStringLiteral("compressed.ove"));
	QFile f(compressed);
	ASSERT_TRUE(f.open(QFile::WriteOnly));
	f.write("OVEC");
	f.write("payload");
	f.close();

	QFile check(compressed);
	ASSERT_TRUE(check.open(QFile::ReadOnly));
	EXPECT_TRUE(olive::ProjectSerializer::check_compressed_id(&check));
	check.close();

	const QString plain =
		QDir(dir.path()).filePath(QStringLiteral("plain.ove"));
	QFile p(plain);
	ASSERT_TRUE(p.open(QFile::WriteOnly));
	p.write("<?xml version=\"1.0\"?>");
	p.close();

	QFile check_plain(plain);
	ASSERT_TRUE(check_plain.open(QFile::ReadOnly));
	EXPECT_FALSE(olive::ProjectSerializer::check_compressed_id(&check_plain));
	check_plain.close();
}

TEST(ProjectSerializer, SaveToInvalidDirectoryFails)
{
	olive::ColorManager::set_up_default_config();
	bool created_disk_manager = false;
	ensure_disk_manager(&created_disk_manager);
	olive::ProjectSerializer::initialize();

	olive::Project project;
	project.initialize();

	olive::ProjectSerializer::Result result = olive::ProjectSerializer::save(
		olive::ProjectSerializer::SaveData(
			olive::ProjectSerializer::k_project, &project,
			QStringLiteral("/definitely/nonexistent/project.ove")),
		false);

	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_file_error);
	EXPECT_FALSE(result.get_details().isEmpty());

	olive::ProjectSerializer::destroy();
	release_disk_manager(created_disk_manager);
}
