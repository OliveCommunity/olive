#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#include <QDataStream>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "common/qtutils.h"
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/globals.h"
#include "node/output/track/track.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "olive/core/render/samplebuffer.h"
#include "render/loopmode.h"

namespace
{

olive::Track *create_track(olive::Project *project,
						  olive::Track::Type type = olive::Track::k_video)
{
	auto *track = new olive::Track();
	track->setParent(project);
	track->set_type(type);
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

olive::GapBlock *create_gap(olive::Project *project,
						   const olive::core::Rational &length)
{
	auto *gap = new olive::GapBlock();
	gap->setParent(project);
	gap->set_length_and_media_out(length);
	return gap;
}

float sum_of_absolute_samples(const olive::core::SampleBuffer &buffer)
{
	float sum = 0.0f;
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		const float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			sum += std::abs(data[i]);
		}
	}
	return sum;
}

} // namespace

TEST(Track, DefaultState)
{
	olive::Track track;

	EXPECT_EQ(track.type(), olive::Track::k_none);
	EXPECT_EQ(track.index(), -1);
	EXPECT_TRUE(track.blocks().isEmpty());
	EXPECT_EQ(track.track_length(), olive::core::Rational(0));
	EXPECT_FALSE(track.is_muted());
	EXPECT_FALSE(track.is_locked());
	EXPECT_DOUBLE_EQ(track.get_track_height(), olive::Track::k_track_height_default);
	EXPECT_EQ(track.sequence(), nullptr);
	EXPECT_EQ(track.name(), QStringLiteral("Track"));
	EXPECT_EQ(track.id(), QStringLiteral("org.olivevideoeditor.Olive.track"));
	EXPECT_TRUE(track.category().contains(olive::Node::k_category_timeline));
}

TEST(Track, NameReflectsTypeAndIndex)
{
	olive::Track track;

	track.set_type(olive::Track::k_video);
	track.set_index(2);
	EXPECT_EQ(track.name(), QStringLiteral("Video Track 2"));

	track.set_type(olive::Track::k_audio);
	track.set_index(0);
	EXPECT_EQ(track.name(), QStringLiteral("Audio Track 0"));

	track.set_type(olive::Track::k_subtitle);
	track.set_index(1);
	EXPECT_EQ(track.name(), QStringLiteral("Subtitle Track 1"));

	track.set_type(olive::Track::k_none);
	EXPECT_EQ(track.name(), QStringLiteral("Track"));
}

TEST(Track, SetIndexEmitsIndexChanged)
{
	olive::Track track;

	int emissions = 0;
	int old_index = 0;
	int new_index = 0;
	QObject::connect(&track, &olive::Track::index_changed,
					 [&emissions, &old_index, &new_index](int old_i, int now_i) {
						 ++emissions;
						 old_index = old_i;
						 new_index = now_i;
					 });

	track.set_index(3);

	EXPECT_EQ(track.index(), 3);
	EXPECT_EQ(emissions, 1);
	EXPECT_EQ(old_index, -1);
	EXPECT_EQ(new_index, 3);
}

TEST(Track, MuteAndLockToggle)
{
	olive::Track track;

	int emissions = 0;
	bool last_muted = false;
	QObject::connect(&track, &olive::Track::muted_changed,
					 [&emissions, &last_muted](bool e) {
						 ++emissions;
						 last_muted = e;
					 });

	track.set_muted(true);
	EXPECT_TRUE(track.is_muted());
	EXPECT_EQ(emissions, 1);
	EXPECT_TRUE(last_muted);

	track.set_muted(false);
	EXPECT_FALSE(track.is_muted());
	EXPECT_EQ(emissions, 2);
	EXPECT_FALSE(last_muted);

	EXPECT_FALSE(track.is_locked());
	track.set_locked(true);
	EXPECT_TRUE(track.is_locked());
	track.set_locked(false);
	EXPECT_FALSE(track.is_locked());
}

TEST(Track, TrackHeightAccessors)
{
	olive::Track track;

	int emissions = 0;
	qreal last_height = 0.0;
	QObject::connect(&track, &olive::Track::track_height_changed,
					 [&emissions, &last_height](qreal h) {
						 ++emissions;
						 last_height = h;
					 });

	track.set_track_height(2.5);
	EXPECT_DOUBLE_EQ(track.get_track_height(), 2.5);
	EXPECT_EQ(emissions, 1);
	EXPECT_DOUBLE_EQ(last_height, 2.5);

	EXPECT_DOUBLE_EQ(olive::Track::k_track_height_default, 3.0);
	EXPECT_DOUBLE_EQ(olive::Track::k_track_height_minimum, 1.5);
	EXPECT_DOUBLE_EQ(olive::Track::k_track_height_interval, 0.5);
	EXPECT_LT(olive::Track::get_minimum_track_height_in_pixels(),
			  olive::Track::get_default_track_height_in_pixels());
}

TEST(Track, TrackHeightPixelRoundTrip)
{
	olive::Track track;

	track.set_track_height_in_pixels(77);
	EXPECT_EQ(track.get_track_height_in_pixels(), 77);

	olive::Track default_track;
	EXPECT_EQ(default_track.get_track_height_in_pixels(),
			  olive::Track::get_default_track_height_in_pixels());
}

TEST(Track, HeightSaveLoadRoundTrip)
{
	olive::Track track;
	track.set_track_height(1.75);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	track.save_custom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	EXPECT_TRUE(xml.contains(QStringLiteral("<height>1.75</height>")));

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));

	olive::Track loaded;
	ASSERT_TRUE(loaded.load_custom(&reader, nullptr));
	EXPECT_DOUBLE_EQ(loaded.get_track_height(), 1.75);
}

TEST(Track, ReferenceStringsRoundTrip)
{
	const olive::Track::Reference video(olive::Track::k_video, 2);
	EXPECT_EQ(video.to_string(), QStringLiteral("v:2"));
	EXPECT_TRUE(video.is_valid());
	EXPECT_EQ(olive::Track::Reference::from_string(QStringLiteral("v:2")), video);

	const olive::Track::Reference audio(olive::Track::k_audio, 10);
	EXPECT_EQ(audio.to_string(), QStringLiteral("a:10"));
	EXPECT_EQ(olive::Track::Reference::from_string(QStringLiteral("a:10")),
			  audio);

	const olive::Track::Reference subtitle(olive::Track::k_subtitle, 0);
	EXPECT_EQ(subtitle.to_string(), QStringLiteral("s:0"));
	EXPECT_EQ(olive::Track::Reference::from_string(QStringLiteral("s:0")),
			  subtitle);

	EXPECT_EQ(olive::Track::Reference::type_from_string(QStringLiteral("v:2")),
			  olive::Track::k_video);
	EXPECT_EQ(olive::Track::Reference::type_from_string(QStringLiteral("a:3")),
			  olive::Track::k_audio);
	EXPECT_EQ(olive::Track::Reference::type_from_string(QStringLiteral("s:0")),
			  olive::Track::k_subtitle);

	EXPECT_TRUE(
		olive::Track::Reference::type_to_string(olive::Track::k_none).isEmpty());
	EXPECT_TRUE(olive::Track::Reference().to_string().isEmpty());
}

TEST(Track, ReferenceInvalidStrings)
{
	EXPECT_FALSE(
		olive::Track::Reference::from_string(QString()).is_valid());
	EXPECT_FALSE(
		olive::Track::Reference::from_string(QStringLiteral("x:1")).is_valid());
	// Too short to contain a type prefix and separator
	EXPECT_FALSE(
		olive::Track::Reference::from_string(QStringLiteral("v")).is_valid());
	// Non-numeric index fails to parse
	EXPECT_FALSE(
		olive::Track::Reference::from_string(QStringLiteral("v:x")).is_valid());
	EXPECT_EQ(
		olive::Track::Reference::type_from_string(QStringLiteral("q:0")),
		olive::Track::k_none);

	EXPECT_FALSE(olive::Track::Reference().is_valid());
	EXPECT_FALSE(
		olive::Track::Reference(olive::Track::k_count, 0).is_valid());
	EXPECT_FALSE(
		olive::Track::Reference(olive::Track::k_video, -1).is_valid());
}

TEST(Track, ReferenceComparisonAndHash)
{
	const olive::Track::Reference v1(olive::Track::k_video, 1);
	const olive::Track::Reference v1_copy(olive::Track::k_video, 1);
	const olive::Track::Reference v2(olive::Track::k_video, 2);
	const olive::Track::Reference a1(olive::Track::k_audio, 1);

	EXPECT_EQ(v1, v1_copy);
	EXPECT_NE(v1, v2);
	EXPECT_NE(v1, a1);

	// Ordering is by type first, then index
	EXPECT_LT(v1, v2);
	EXPECT_LT(v2, a1);
	EXPECT_FALSE(a1 < v2);

	EXPECT_EQ(olive::qHash(v1), olive::qHash(v1_copy));
}

TEST(Track, ReferenceDataStreamRoundTrip)
{
	const olive::Track::Reference ref(olive::Track::k_audio, 5);

	QByteArray bytes;
	QDataStream out(&bytes, QIODevice::WriteOnly);
	out << ref;

	QDataStream in(bytes);
	olive::Track::Reference loaded;
	in >> loaded;

	EXPECT_EQ(loaded, ref);
}

TEST(Track, ToReferenceMatchesTypeAndIndex)
{
	olive::Track track;
	track.set_type(olive::Track::k_audio);
	track.set_index(7);

	const olive::Track::Reference ref = track.to_reference();
	EXPECT_EQ(ref, olive::Track::Reference(olive::Track::k_audio, 7));
	EXPECT_EQ(ref.to_string(), QStringLiteral("a:7"));
}

TEST(Track, AppendBlocksSetsInOutLengthAndLinks)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	int added_count = 0;
	olive::Block *last_added = nullptr;
	QObject::connect(track, &olive::Track::block_added,
					 [&added_count, &last_added](olive::Block *b) {
						 ++added_count;
						 last_added = b;
					 });
	int length_changed_count = 0;
	QObject::connect(track, &olive::Track::track_length_changed,
					 [&length_changed_count]() { ++length_changed_count; });

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);

	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);

	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(b->out(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));

	EXPECT_EQ(a->track(), track);
	EXPECT_EQ(b->track(), track);
	EXPECT_EQ(a->previous(), nullptr);
	EXPECT_EQ(a->next(), b);
	EXPECT_EQ(b->previous(), a);
	EXPECT_EQ(b->next(), nullptr);

	EXPECT_EQ(added_count, 2);
	EXPECT_EQ(last_added, b);
	EXPECT_EQ(length_changed_count, 2);
}

TEST(Track, PrependBlockShiftsExistingBlocks)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(2));
	track->append_block(b);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(1));
	track->prepend_block(a);

	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);

	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(1));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(3));

	EXPECT_EQ(a->next(), b);
	EXPECT_EQ(b->previous(), a);
}

TEST(Track, InsertBlockAtIndexMiddle)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(1));
	olive::ClipBlock *c = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	track->append_block(c);

	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(2));
	track->insert_block_at_index(b, 1);

	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), b);
	EXPECT_EQ(track->blocks().at(2), c);

	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(1));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	EXPECT_EQ(c->in(), olive::core::Rational(3));
	EXPECT_EQ(c->out(), olive::core::Rational(4));

	EXPECT_EQ(a->next(), b);
	EXPECT_EQ(b->previous(), a);
	EXPECT_EQ(b->next(), c);
	EXPECT_EQ(c->previous(), b);
}

TEST(Track, InsertBlockAfterAndBefore)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(1));
	olive::ClipBlock *c = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	track->append_block(c);

	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(2));
	track->insert_block_after(b, a);

	olive::ClipBlock *d = create_clip(&project, olive::core::Rational(1));
	track->insert_block_before(d, c);

	// A null "before" block prepends, a null "after" block appends
	olive::ClipBlock *e = create_clip(&project, olive::core::Rational(1));
	track->insert_block_after(e, nullptr);

	olive::ClipBlock *f = create_clip(&project, olive::core::Rational(1));
	track->insert_block_before(f, nullptr);

	const QVector<olive::Block *> expected = { e, a, b, d, c, f };
	ASSERT_EQ(track->blocks().size(), expected.size());
	for (int i = 0; i < expected.size(); ++i) {
		EXPECT_EQ(track->blocks().at(i), expected.at(i));
	}

	EXPECT_EQ(e->in(), olive::core::Rational(0));
	EXPECT_EQ(a->in(), olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(2));
	EXPECT_EQ(d->in(), olive::core::Rational(4));
	EXPECT_EQ(c->in(), olive::core::Rational(5));
	EXPECT_EQ(f->in(), olive::core::Rational(6));
	EXPECT_EQ(f->out(), olive::core::Rational(7));
	EXPECT_EQ(track->track_length(), olive::core::Rational(7));
}

TEST(Track, RippleRemoveBlockShiftsAndDetaches)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(3));
	olive::ClipBlock *c = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);
	ASSERT_EQ(track->track_length(), olive::core::Rational(6));

	int removed_count = 0;
	olive::Block *last_removed = nullptr;
	QObject::connect(track, &olive::Track::block_removed,
					 [&removed_count, &last_removed](olive::Block *blk) {
						 ++removed_count;
						 last_removed = blk;
					 });

	track->ripple_remove_block(b);

	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), c);

	// Subsequent blocks move earlier to fill the space
	EXPECT_EQ(a->in(), olive::core::Rational(0));
	EXPECT_EQ(a->out(), olive::core::Rational(2));
	EXPECT_EQ(c->in(), olive::core::Rational(2));
	EXPECT_EQ(c->out(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(3));

	// The removed block is detached and reset to zero-based in/out
	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(b->previous(), nullptr);
	EXPECT_EQ(b->next(), nullptr);
	EXPECT_EQ(b->in(), olive::core::Rational(0));
	EXPECT_EQ(b->out(), b->length());

	EXPECT_EQ(a->next(), c);
	EXPECT_EQ(c->previous(), a);

	EXPECT_EQ(removed_count, 1);
	EXPECT_EQ(last_removed, b);
}

TEST(Track, RippleRemoveFirstAndLastBlock)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(3));
	olive::ClipBlock *c = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);

	track->ripple_remove_block(a);
	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(b->in(), olive::core::Rational(0));
	EXPECT_EQ(b->out(), olive::core::Rational(3));
	EXPECT_EQ(c->in(), olive::core::Rational(3));
	EXPECT_EQ(track->track_length(), olive::core::Rational(4));
	EXPECT_EQ(b->previous(), nullptr);

	track->ripple_remove_block(c);
	ASSERT_EQ(track->blocks().size(), 1);
	EXPECT_EQ(track->track_length(), olive::core::Rational(3));
	EXPECT_EQ(b->next(), nullptr);

	track->ripple_remove_block(b);
	EXPECT_TRUE(track->blocks().isEmpty());
	EXPECT_EQ(track->track_length(), olive::core::Rational(0));
}

TEST(Track, ReplaceBlockSameLength)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);

	int removed_count = 0;
	int added_count = 0;
	QObject::connect(track, &olive::Track::block_removed,
					 [&removed_count](olive::Block *) { ++removed_count; });
	QObject::connect(track, &olive::Track::block_added,
					 [&added_count](olive::Block *) { ++added_count; });

	olive::ClipBlock *r = create_clip(&project, olive::core::Rational(3));
	track->replace_block(b, r);

	ASSERT_EQ(track->blocks().size(), 2);
	EXPECT_EQ(track->blocks().at(0), a);
	EXPECT_EQ(track->blocks().at(1), r);

	// Same-length replacement keeps in/out points identical
	EXPECT_EQ(r->in(), olive::core::Rational(2));
	EXPECT_EQ(r->out(), olive::core::Rational(5));
	EXPECT_EQ(track->track_length(), olive::core::Rational(5));

	EXPECT_EQ(b->track(), nullptr);
	EXPECT_EQ(b->previous(), nullptr);
	EXPECT_EQ(b->next(), nullptr);
	EXPECT_EQ(r->track(), track);
	EXPECT_EQ(r->previous(), a);
	EXPECT_EQ(r->next(), nullptr);
	EXPECT_EQ(a->next(), r);

	EXPECT_EQ(removed_count, 1);
	EXPECT_EQ(added_count, 1);
}

TEST(Track, ReplaceBlockDifferentLengthRipples)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *c = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);
	ASSERT_EQ(track->track_length(), olive::core::Rational(5));

	olive::ClipBlock *r = create_clip(&project, olive::core::Rational(4));
	track->replace_block(b, r);

	ASSERT_EQ(track->blocks().size(), 3);
	EXPECT_EQ(r->in(), olive::core::Rational(2));
	EXPECT_EQ(r->out(), olive::core::Rational(6));
	// The longer replacement pushes subsequent blocks later
	EXPECT_EQ(c->in(), olive::core::Rational(6));
	EXPECT_EQ(c->out(), olive::core::Rational(7));
	EXPECT_EQ(track->track_length(), olive::core::Rational(7));
}

TEST(Track, BlockLookupFunctions)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::GapBlock *g = create_gap(&project, olive::core::Rational(3));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(4));
	track->append_block(a);
	track->append_block(g);
	track->append_block(b);
	// Layout: a [0,2], g [2,5], b [5,9]

	// BlockContainingTime: strictly inside only, never at edges
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(1)), a);
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(0)), nullptr);
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(2)), nullptr);
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(3)), g);
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(5)), nullptr);
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(8)), b);
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(9)), nullptr);
	EXPECT_EQ(track->block_containing_time(olive::core::Rational(100)), nullptr);

	// NearestBlockBefore: block starting before and ending at/after the time
	EXPECT_EQ(track->nearest_block_before(olive::core::Rational(2)), a);
	EXPECT_EQ(track->nearest_block_before(olive::core::Rational(5)), g);
	EXPECT_EQ(track->nearest_block_before(olive::core::Rational(9)), b);
	EXPECT_EQ(track->nearest_block_before(olive::core::Rational(10)), nullptr);

	// NearestBlockBeforeOrAt: first block ending after the time
	EXPECT_EQ(track->nearest_block_before_or_at(olive::core::Rational(0)), a);
	EXPECT_EQ(track->nearest_block_before_or_at(olive::core::Rational(2)), g);
	EXPECT_EQ(track->nearest_block_before_or_at(olive::core::Rational(5)), b);
	EXPECT_EQ(track->nearest_block_before_or_at(olive::core::Rational(9)), nullptr);

	// NearestBlockAfterOrAt: first block starting at or after the time
	EXPECT_EQ(track->nearest_block_after_or_at(olive::core::Rational(0)), a);
	EXPECT_EQ(track->nearest_block_after_or_at(olive::core::Rational(2)), g);
	EXPECT_EQ(track->nearest_block_after_or_at(olive::core::Rational(5)), b);
	EXPECT_EQ(track->nearest_block_after_or_at(olive::core::Rational(9)), nullptr);

	// NearestBlockAfter: first block starting strictly after the time
	EXPECT_EQ(track->nearest_block_after(olive::core::Rational(0)), g);
	EXPECT_EQ(track->nearest_block_after(olive::core::Rational(2)), b);
	EXPECT_EQ(track->nearest_block_after(olive::core::Rational(4)), b);
	EXPECT_EQ(track->nearest_block_after(olive::core::Rational(5)), nullptr);

	// VisibleBlockAtTime: half-open [in, out) containment via binary search
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(0)), a);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(1)), a);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(2)), g);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(4)), g);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(5)), b);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(8)), b);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(9)), nullptr);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(-1)), nullptr);
	EXPECT_EQ(track->visible_block_at_time(olive::core::Rational(100)), nullptr);
}

TEST(Track, BlockLookupOnEmptyTrack)
{
	olive::Track track;

	EXPECT_EQ(track.block_containing_time(olive::core::Rational(0)), nullptr);
	EXPECT_EQ(track.nearest_block_before(olive::core::Rational(0)), nullptr);
	EXPECT_EQ(track.nearest_block_before_or_at(olive::core::Rational(0)), nullptr);
	EXPECT_EQ(track.nearest_block_after_or_at(olive::core::Rational(0)), nullptr);
	EXPECT_EQ(track.nearest_block_after(olive::core::Rational(0)), nullptr);
	EXPECT_EQ(track.visible_block_at_time(olive::core::Rational(0)), nullptr);
	EXPECT_EQ(track.track_length(), olive::core::Rational(0));
}

TEST(Track, IsRangeFree)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::GapBlock *g = create_gap(&project, olive::core::Rational(3));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(4));
	track->append_block(a);
	track->append_block(g);
	track->append_block(b);
	// Layout: clip a [0,2], gap g [2,5], clip b [5,9]

	// A range covering only the gap (or part of it) is free
	EXPECT_TRUE(track->is_range_free(olive::core::TimeRange(
		olive::core::Rational(2), olive::core::Rational(5))));
	EXPECT_TRUE(track->is_range_free(olive::core::TimeRange(
		olive::core::Rational(3), olive::core::Rational(4))));

	// Ranges touching clips are not free
	EXPECT_FALSE(track->is_range_free(olive::core::TimeRange(
		olive::core::Rational(0), olive::core::Rational(2))));
	EXPECT_FALSE(track->is_range_free(olive::core::TimeRange(
		olive::core::Rational(1), olive::core::Rational(3))));
	EXPECT_FALSE(track->is_range_free(olive::core::TimeRange(
		olive::core::Rational(4), olive::core::Rational(9))));

	// Past the end of the track there is nothing in the way
	EXPECT_TRUE(track->is_range_free(olive::core::TimeRange(
		olive::core::Rational(9), olive::core::Rational(12))));

	olive::Track empty_track;
	EXPECT_TRUE(empty_track.is_range_free(olive::core::TimeRange(
		olive::core::Rational(0), olive::core::Rational(10))));
}

TEST(Track, ArrayIndexesAreReusedAfterRemoval)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(1));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(1));
	olive::ClipBlock *c = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	track->append_block(c);

	EXPECT_EQ(track->get_array_index_from_block(a), 0);
	EXPECT_EQ(track->get_array_index_from_block(b), 1);
	EXPECT_EQ(track->get_array_index_from_block(c), 2);

	// Removing B frees its input array index; the next block reuses it
	track->ripple_remove_block(b);

	olive::ClipBlock *d = create_clip(&project, olive::core::Rational(1));
	track->append_block(d);

	EXPECT_EQ(track->get_array_index_from_block(a), 0);
	EXPECT_EQ(track->get_array_index_from_block(c), 2);
	EXPECT_EQ(track->get_array_index_from_block(d), 1);

	// The array map standard value mirrors the cache order [a, c, d]
	const QByteArray bytes =
		track->get_standard_value(olive::Track::k_array_map_input).toByteArray();
	ASSERT_EQ(bytes.size(), 3 * int(sizeof(uint32_t)));
	uint32_t values[3];
	std::memcpy(values, bytes.constData(), sizeof(values));
	EXPECT_EQ(values[0], 0u);
	EXPECT_EQ(values[1], 2u);
	EXPECT_EQ(values[2], 1u);
}

TEST(Track, BlockLengthChangeRipplesSubsequentBlocks)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(2));
	track->append_block(a);
	track->append_block(b);
	ASSERT_EQ(track->track_length(), olive::core::Rational(4));

	int refreshed_count = 0;
	QObject::connect(track, &olive::Track::blocks_refreshed,
					 [&refreshed_count]() { ++refreshed_count; });

	a->set_length_and_media_out(olive::core::Rational(5));
	EXPECT_EQ(a->out(), olive::core::Rational(5));
	EXPECT_EQ(b->in(), olive::core::Rational(5));
	EXPECT_EQ(b->out(), olive::core::Rational(7));
	EXPECT_EQ(track->track_length(), olive::core::Rational(7));

	b->set_length_and_media_out(olive::core::Rational(1));
	EXPECT_EQ(b->in(), olive::core::Rational(5));
	EXPECT_EQ(b->out(), olive::core::Rational(6));
	EXPECT_EQ(track->track_length(), olive::core::Rational(6));

	EXPECT_EQ(refreshed_count, 2);
}

TEST(Track, GetActiveElementsAtTime)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	// An empty track has no active elements
	EXPECT_EQ(track->get_active_elements_at_time(
					  olive::Track::k_block_input,
					  olive::core::TimeRange(olive::core::Rational(0),
											 olive::core::Rational(1)))
					  .mode(),
				  olive::Node::ActiveElements::k_no_elements);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::GapBlock *g = create_gap(&project, olive::core::Rational(2));
	track->append_block(a);
	track->append_block(g);
	// Layout: clip a [0,2], gap g [2,4]

	// The clip is the only active element in [0,1]
	const olive::Node::ActiveElements active =
		track->get_active_elements_at_time(
			olive::Track::k_block_input,
			olive::core::TimeRange(olive::core::Rational(0),
								   olive::core::Rational(1)));
	EXPECT_EQ(active.mode(), olive::Node::ActiveElements::k_specified);
	ASSERT_EQ(active.elements().size(), 1);
	EXPECT_EQ(active.elements().front(), track->get_array_index_from_block(a));

	// Gaps never count as active elements
	EXPECT_EQ(track->get_active_elements_at_time(
					  olive::Track::k_block_input,
					  olive::core::TimeRange(olive::core::Rational(2),
											 olive::core::Rational(4)))
					  .mode(),
				  olive::Node::ActiveElements::k_no_elements);

	// A muted track reports no active elements
	track->set_muted(true);
	EXPECT_EQ(track->get_active_elements_at_time(
					  olive::Track::k_block_input,
					  olive::core::TimeRange(olive::core::Rational(0),
											 olive::core::Rational(1)))
					  .mode(),
				  olive::Node::ActiveElements::k_no_elements);
	track->set_muted(false);

	// A disabled clip is not an active element
	a->set_enabled(false);
	EXPECT_EQ(track->get_active_elements_at_time(
					  olive::Track::k_block_input,
					  olive::core::TimeRange(olive::core::Rational(0),
											 olive::core::Rational(1)))
					  .mode(),
				  olive::Node::ActiveElements::k_no_elements);

	// Ranges outside the track length have no active elements
	EXPECT_EQ(track->get_active_elements_at_time(
					  olive::Track::k_block_input,
					  olive::core::TimeRange(olive::core::Rational(4),
											 olive::core::Rational(8)))
					  .mode(),
				  olive::Node::ActiveElements::k_no_elements);
	EXPECT_EQ(track->get_active_elements_at_time(
					  olive::Track::k_block_input,
					  olive::core::TimeRange(olive::core::Rational(-1),
											 olive::core::Rational(0)))
					  .mode(),
				  olive::Node::ActiveElements::k_no_elements);

	// Non-block inputs fall back to the default (all elements)
	EXPECT_EQ(track->get_active_elements_at_time(
					  olive::Track::k_muted_input,
					  olive::core::TimeRange(olive::core::Rational(0),
											 olive::core::Rational(1)))
					  .mode(),
				  olive::Node::ActiveElements::k_all_elements);
}

TEST(Track, TimeAdjustmentTransformsAroundBlock)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,2], b [2,5]

	const int b_index = track->get_array_index_from_block(b);

	// Without clamping, the range is simply shifted by the block's in point
	EXPECT_EQ(track->input_time_adjustment(
				  olive::Track::k_block_input, b_index,
				  olive::core::TimeRange(olive::core::Rational(2),
										 olive::core::Rational(5)),
				  false),
			  olive::core::TimeRange(olive::core::Rational(0),
									 olive::core::Rational(3)));

	// Clamping limits the range to the block before shifting
	EXPECT_EQ(track->input_time_adjustment(
				  olive::Track::k_block_input, b_index,
				  olive::core::TimeRange(olive::core::Rational(0),
										 olive::core::Rational(10)),
				  true),
			  olive::core::TimeRange(olive::core::Rational(0),
									 olive::core::Rational(3)));
	EXPECT_EQ(track->input_time_adjustment(
				  olive::Track::k_block_input, b_index,
				  olive::core::TimeRange(olive::core::Rational(1),
										 olive::core::Rational(4)),
				  true),
			  olive::core::TimeRange(olive::core::Rational(0),
									 olive::core::Rational(2)));

	// Output adjustment shifts block-local time back into track time
	EXPECT_EQ(track->output_time_adjustment(
				  olive::Track::k_block_input, b_index,
				  olive::core::TimeRange(olive::core::Rational(0),
										 olive::core::Rational(3))),
			  olive::core::TimeRange(olive::core::Rational(2),
									 olive::core::Rational(5)));

	// Unknown elements and inputs pass the range through unchanged
	const olive::core::TimeRange unchanged(olive::core::Rational(1),
										   olive::core::Rational(2));
	EXPECT_EQ(track->input_time_adjustment(olive::Track::k_block_input, 99,
										 unchanged, true),
			  unchanged);
	EXPECT_EQ(track->output_time_adjustment(olive::Track::k_block_input, 99,
										  unchanged),
			  unchanged);
	EXPECT_EQ(track->input_time_adjustment(olive::Track::k_muted_input, -1,
										 unchanged, true),
			  unchanged);
}

TEST(Track, TransformTimeHelpersRespectInfinities)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(2));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(3));
	track->append_block(a);
	track->append_block(b);
	// b occupies [2,5]

	const olive::core::Rational k_max(INT_MAX);
	const olive::core::Rational k_min(INT_MIN);

	EXPECT_EQ(olive::Track::transform_time_for_block(b, k_max), k_max);
	EXPECT_EQ(olive::Track::transform_time_for_block(b, k_min), k_min);
	EXPECT_EQ(olive::Track::transform_time_from_block(b, k_max), k_max);
	EXPECT_EQ(olive::Track::transform_time_from_block(b, k_min), k_min);

	EXPECT_EQ(olive::Track::transform_time_for_block(b, olive::core::Rational(3)),
			  olive::core::Rational(1));
	EXPECT_EQ(
		olive::Track::transform_time_from_block(b, olive::core::Rational(1)),
		olive::core::Rational(3));

	EXPECT_EQ(olive::Track::transform_range_for_block(
				  b, olive::core::TimeRange(olive::core::Rational(2),
											olive::core::Rational(5))),
			  olive::core::TimeRange(olive::core::Rational(0),
									 olive::core::Rational(3)));
	EXPECT_EQ(olive::Track::transform_range_from_block(
				  b, olive::core::TimeRange(olive::core::Rational(0),
											olive::core::Rational(3))),
			  olive::core::TimeRange(olive::core::Rational(2),
									 olive::core::Rational(5)));
}

TEST(Track, VideoValuePassesThroughFirstArrayElement)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project, olive::Track::k_video);

	olive::NodeValueArray arr;
	arr.emplace(0, olive::NodeValue(olive::NodeValue::k_float, 42.0));

	olive::NodeValueRow row;
	row.insert(olive::Track::k_block_input,
			   olive::NodeValue(olive::NodeValue::k_none, arr, nullptr, true));

	olive::NodeValueTable table;
	track->value(row, olive::NodeGlobals(), &table);

	const olive::NodeValue out = table.get(olive::NodeValue::k_float);
	ASSERT_EQ(out.type(), olive::NodeValue::k_float);
	EXPECT_DOUBLE_EQ(out.to_double(), 42.0);

	// An empty block array pushes nothing
	olive::NodeValueRow empty_row;
	empty_row.insert(olive::Track::k_block_input,
					 olive::NodeValue(olive::NodeValue::k_none,
									  olive::NodeValueArray(), nullptr, true));

	olive::NodeValueTable empty_table;
	track->value(empty_row, olive::NodeGlobals(), &empty_table);
	EXPECT_EQ(empty_table.get(olive::NodeValue::k_float).type(),
			  olive::NodeValue::k_none);
}

TEST(Track, AudioValueProducesSilentBufferWithoutBlocks)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project, olive::Track::k_audio);

	const olive::core::AudioParams aparams(48000,
										   olive::core::k_channel_layout_stereo,
										   olive::core::SampleFormat::f32_p);

	olive::NodeValueRow row;
	row.insert(olive::Track::k_block_input,
			   olive::NodeValue(olive::NodeValue::k_none,
								olive::NodeValueArray(), nullptr, true));

	const olive::NodeGlobals globals(
		olive::VideoParams(), aparams,
		olive::core::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(1, 2)),
		olive::LoopMode::k_loop_mode_off);

	olive::NodeValueTable table;
	track->value(row, globals, &table);

	const olive::NodeValue out = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(out.type(), olive::NodeValue::k_samples);

	const olive::core::SampleBuffer samples = out.to_samples();
	ASSERT_TRUE(samples.is_allocated());
	EXPECT_EQ(samples.channel_count(), 2);
	EXPECT_EQ(samples.sample_count(), 24000);
	EXPECT_FLOAT_EQ(sum_of_absolute_samples(samples), 0.0f);
}

TEST(Track, AudioValueMixesBlockSamples)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project, olive::Track::k_audio);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(1));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	track->append_block(b);
	// Layout: a [0,1], b [1,2]

	const olive::core::AudioParams aparams(48000,
										   olive::core::k_channel_layout_stereo,
										   olive::core::SampleFormat::f32_p);

	olive::core::SampleBuffer a_samples(aparams, olive::core::Rational(1));
	olive::core::SampleBuffer b_samples(aparams, olive::core::Rational(1));
	ASSERT_TRUE(a_samples.is_allocated());
	ASSERT_TRUE(b_samples.is_allocated());
	for (int ch = 0; ch < a_samples.channel_count(); ++ch) {
		std::fill(a_samples.data(ch), a_samples.data(ch) + 48000, 1.0f);
		std::fill(b_samples.data(ch), b_samples.data(ch) + 48000, 0.5f);
	}

	olive::NodeValueArray arr;
	arr.emplace(track->get_array_index_from_block(a),
				olive::NodeValue(olive::NodeValue::k_samples, a_samples));
	arr.emplace(track->get_array_index_from_block(b),
				olive::NodeValue(olive::NodeValue::k_samples, b_samples));

	olive::NodeValueRow row;
	row.insert(olive::Track::k_block_input,
			   olive::NodeValue(olive::NodeValue::k_none, arr, nullptr, true));

	const olive::NodeGlobals globals(
		olive::VideoParams(), aparams,
		olive::core::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(2)),
		olive::LoopMode::k_loop_mode_off);

	olive::NodeValueTable table;
	track->value(row, globals, &table);

	const olive::NodeValue out = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(out.type(), olive::NodeValue::k_samples);

	const olive::core::SampleBuffer mixed = out.to_samples();
	ASSERT_TRUE(mixed.is_allocated());
	ASSERT_EQ(mixed.sample_count(), 96000);

	// Each block's samples land at their track position
	EXPECT_FLOAT_EQ(mixed.data(0)[0], 1.0f);
	EXPECT_FLOAT_EQ(mixed.data(0)[47999], 1.0f);
	EXPECT_FLOAT_EQ(mixed.data(0)[48000], 0.5f);
	EXPECT_FLOAT_EQ(mixed.data(0)[95999], 0.5f);
	EXPECT_FLOAT_EQ(mixed.data(1)[24000], 1.0f);
	EXPECT_FLOAT_EQ(mixed.data(1)[72000], 0.5f);
}

TEST(Track, AudioValueSilencesZeroSpeedClip)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project, olive::Track::k_audio);

	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(1));
	track->append_block(a);
	a->set_standard_value(olive::ClipBlock::k_speed_input, 0.0);
	ASSERT_DOUBLE_EQ(a->speed(), 0.0);

	const olive::core::AudioParams aparams(48000,
										   olive::core::k_channel_layout_stereo,
										   olive::core::SampleFormat::f32_p);

	olive::core::SampleBuffer a_samples(aparams, olive::core::Rational(1));
	ASSERT_TRUE(a_samples.is_allocated());
	for (int ch = 0; ch < a_samples.channel_count(); ++ch) {
		std::fill(a_samples.data(ch), a_samples.data(ch) + 48000, 1.0f);
	}

	olive::NodeValueArray arr;
	arr.emplace(track->get_array_index_from_block(a),
				olive::NodeValue(olive::NodeValue::k_samples, a_samples));

	olive::NodeValueRow row;
	row.insert(olive::Track::k_block_input,
			   olive::NodeValue(olive::NodeValue::k_none, arr, nullptr, true));

	const olive::NodeGlobals globals(
		olive::VideoParams(), aparams,
		olive::core::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(1)),
		olive::LoopMode::k_loop_mode_off);

	olive::NodeValueTable table;
	track->value(row, globals, &table);

	const olive::NodeValue out = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(out.type(), olive::NodeValue::k_samples);

	// Zero-speed audio is defined to come out as silence
	const olive::core::SampleBuffer samples = out.to_samples();
	ASSERT_TRUE(samples.is_allocated());
	EXPECT_FLOAT_EQ(sum_of_absolute_samples(samples), 0.0f);
}

TEST(Track, SubtitleValuePushesNothing)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::Track *track = create_track(&project, olive::Track::k_subtitle);

	olive::NodeValueArray arr;
	arr.emplace(0, olive::NodeValue(olive::NodeValue::k_float, 42.0));

	olive::NodeValueRow row;
	row.insert(olive::Track::k_block_input,
			   olive::NodeValue(olive::NodeValue::k_none, arr, nullptr, true));

	olive::NodeValueTable table;
	track->value(row, olive::NodeGlobals(), &table);
	EXPECT_EQ(table.count(), 0);
}
