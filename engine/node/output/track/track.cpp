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

#include "track.h"

#include <QDebug>
#include <QFontMetrics>

#include "audio/audioprocessor.h"
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/block/transition/transition.h"

namespace olive
{

#define super Node

const double Track::k_track_height_default = 3.0;
const double Track::k_track_height_minimum = 1.5;
const double Track::k_track_height_interval = 0.5;

const QString Track::k_block_input = QStringLiteral("block_in");
const QString Track::k_muted_input = QStringLiteral("muted_in");
const QString Track::k_array_map_input = QStringLiteral("arraymap_in");

Track::Track()
	: track_type_(Track::k_none)
	, index_(-1)
	, locked_(false)
	, sequence_(nullptr)
	, ignore_arraymap_(0)
	, arraymap_invalid_(false)
	, ignore_arraymap_set_(false)
{
	add_input(k_block_input, NodeValue::k_none,
			 InputFlags(k_input_flag_array | k_input_flag_not_keyframable |
						k_input_flag_hidden | k_input_flag_ignore_invalidations));

	add_input(k_muted_input, NodeValue::k_boolean, false,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_array_map_input, NodeValue::k_binary,
			 InputFlags(k_input_flag_static | k_input_flag_hidden |
						k_input_flag_ignore_invalidations));

	// Set default height
	track_height_ = k_track_height_default;
}

void Track::set_type(const Type &track_type)
{
	track_type_ = track_type;
}

const Track::Type &Track::type() const
{
	return track_type_;
}

QString Track::name() const
{
	if (track_type_ == Track::k_video) {
		return tr("Video Track %1").arg(index_);
	} else if (track_type_ == Track::k_audio) {
		return tr("Audio Track %1").arg(index_);
	} else if (track_type_ == Track::k_subtitle) {
		return tr("Subtitle Track %1").arg(index_);
	}

	return tr("Track");
}

QString Track::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.track");
}

QVector<Node::CategoryID> Track::category() const
{
	return { k_category_timeline };
}

QString Track::description() const
{
	return tr(
		"Node for representing and processing a single array of Blocks sorted by time. Also represents the end of "
		"a Sequence.");
}

Node::ActiveElements Track::get_active_elements_at_time(const QString &input,
													const TimeRange &r) const
{
	if (input == k_block_input) {
		if (is_muted() || blocks_.empty() || r.in() >= track_length() ||
			r.out() <= 0) {
			return ActiveElements::k_no_elements;
		} else {
			int start = get_block_index_at_time(r.in());
			int end = get_block_index_at_time(r.out());

			if (start == -1) {
				start = 0;
			}
			if (end == -1) {
				end = blocks_.size() - 1;
			}

			if (blocks_.at(end)->in() == r.out()) {
				end--;
			}

			ActiveElements a;
			for (int i = start; i <= end; i++) {
				Block *b = blocks_.at(i);
				if (b->is_enabled() && (dynamic_cast<ClipBlock *>(b) ||
										dynamic_cast<TransitionBlock *>(b))) {
					a.add(get_array_index_from_cache_index(i));
				}
			}

			if (a.elements().empty()) {
				return ActiveElements::k_no_elements;
			} else {
				return a;
			}
		}
	} else {
		return super::get_active_elements_at_time(input, r);
	}
}

void Track::value(const NodeValueRow &value, const NodeGlobals &globals,
				  NodeValueTable *table) const
{
	if (this->type() == Track::k_video) {
		// Just pass straight through
		NodeValueArray a = value[k_block_input].to_array();
		if (!a.empty()) {
			table->push(a.begin()->second);
		}
	} else if (this->type() == Track::k_audio) {
		// Audio
		process_audio_track(value, globals, table);
	}
}

TimeRange Track::input_time_adjustment(const QString &input, int element,
									 const TimeRange &input_time,
									 bool clamp) const
{
	if (input == k_block_input && element >= 0) {
		int cache_index = get_cache_index_from_array_index(element);

		if (cache_index > -1) {
			TimeRange r = input_time;
			Block *b = blocks_.at(cache_index);

			if (clamp) {
				r.set_range(std::max(r.in(), b->in()),
							std::min(r.out(), b->out()));
			}

			return transform_range_for_block(b, r);
		}
	}

	return Node::input_time_adjustment(input, element, input_time, clamp);
}

TimeRange Track::output_time_adjustment(const QString &input, int element,
									  const TimeRange &input_time) const
{
	if (input == k_block_input && element >= 0) {
		int cache_index = get_cache_index_from_array_index(element);

		if (cache_index > -1) {
			return transform_range_from_block(blocks_.at(cache_index), input_time);
		}
	}

	return Node::output_time_adjustment(input, element, input_time);
}

const double &Track::get_track_height() const
{
	return track_height_;
}

void Track::set_track_height(const double &height)
{
	track_height_ = height;
	emit track_height_changed(track_height_);
}

bool Track::load_custom(QXmlStreamReader *reader, SerializedData *data)
{
	ignore_arraymap_set_ = true;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("height")) {
			this->set_track_height(reader->readElementText().toDouble());
		} else {
			reader->skipCurrentElement();
		}
	}

	return true;
}

void Track::save_custom(QXmlStreamWriter *writer) const
{
	writer->writeTextElement(QStringLiteral("height"),
							 QString::number(this->get_track_height()));
}

void Track::PostLoadEvent(SerializedData *data)
{
	ignore_arraymap_set_ = false;
	refresh_block_cache_from_array_map();
}

void Track::InputValueChangedEvent(const QString &input, int element)
{
	Q_UNUSED(element)

	if (input == k_muted_input) {
		emit muted_changed(is_muted());
	} else if (input == k_array_map_input) {
		if (ignore_arraymap_ > 0) {
			ignore_arraymap_--;
		} else {
			refresh_block_cache_from_array_map();
		}
	}
}

void Track::retranslate()
{
	super::retranslate();

	set_input_name(k_block_input, tr("Blocks"));
	set_input_name(k_muted_input, tr("Muted"));
}

void Track::set_index(const int &index)
{
	int old = index_;

	index_ = index;

	emit index_changed(old, index_);
}

Block *Track::block_containing_time(const Rational &time) const
{
	foreach (Block *block, blocks_) {
		if (block->in() < time && block->out() > time) {
			return block;
		} else if (block->out() == time) {
			break;
		}
	}

	return nullptr;
}

Block *Track::nearest_block_before(const Rational &time) const
{
	foreach (Block *block, blocks_) {
		// Blocks are sorted by time, so the first Block who's out point is at/after this time is the correct Block
		if (block->in() == time) {
			break;
		}

		if (block->out() >= time) {
			return block;
		}
	}

	return nullptr;
}

Block *Track::nearest_block_before_or_at(const Rational &time) const
{
	foreach (Block *block, blocks_) {
		// Blocks are sorted by time, so the first Block who's out point is at/after this time is the correct Block
		if (block->out() > time) {
			return block;
		}
	}

	return nullptr;
}

Block *Track::nearest_block_after_or_at(const Rational &time) const
{
	foreach (Block *block, blocks_) {
		// Blocks are sorted by time, so the first Block after this time is the correct Block
		if (block->in() >= time) {
			return block;
		}
	}

	return nullptr;
}

Block *Track::nearest_block_after(const Rational &time) const
{
	foreach (Block *block, blocks_) {
		// Blocks are sorted by time, so the first Block after this time is the correct Block
		if (block->in() > time) {
			return block;
		}
	}

	return nullptr;
}

bool Track::is_range_free(const TimeRange &range) const
{
	Block *b = nearest_block_before_or_at(range.in());
	if (!b) {
		// No block here, assume track is empty here
		return true;
	}

	if (!dynamic_cast<GapBlock *>(b)) {
		// There's a block at or around the start point that isn't a gap, range is not free
		return false;
	}

	while ((b = b->next())) {
		if (b->in() >= range.out()) {
			// This block is after the range, no longer relevant
			break;
		} else if (!dynamic_cast<GapBlock *>(b)) {
			// Found a block in this range, range is not free
			return false;
		}
	}

	// If we get here, we couldn't find anything in the way of this range
	return true;
}

void Track::invalidate_cache(const TimeRange &range, const QString &from,
							int element, InvalidateCacheOptions options)
{
	TimeRange limited;

	const Block *b;

	if (from == k_block_input && element >= 0 &&
		(b = dynamic_cast<const Block *>(get_connected_output(from, element))) &&
		!options.value(QStringLiteral("lengthevent")).toBool()) {
		// Limit the range signal to the corresponding block
		TimeRange transformed = transform_range_from_block(b, range);

		if (transformed.out() <= b->in() || transformed.in() >= b->out()) {
			return;
		}

		limited = TimeRange(qMax(transformed.in(), b->in()),
							qMin(transformed.out(), b->out()));
	} else {
		limited = range;
	}

	// NOTE: For now, I figure we drop this key, but we may find in the future that it's advantageous
	//       to keep it
	options.remove(QStringLiteral("lengthevent"));

	Node::invalidate_cache(limited, from, element, options);
}

void Track::insert_block_before(Block *block, Block *after)
{
	if (!after) {
		append_block(block);
	} else {
		insert_block_at_index(block, blocks_.indexOf(after));
	}
}

void Track::insert_block_after(Block *block, Block *before)
{
	if (!before) {
		prepend_block(block);
	} else {
		int before_index = blocks_.indexOf(before);

		Q_ASSERT(before_index >= 0);

		insert_block_at_index(block, before_index + 1);
	}
}

void Track::prepend_block(Block *block)
{
	insert_block_at_index(block, 0);
}

void Track::insert_block_at_index(Block *block, int index)
{
	// Set track
	Q_ASSERT(block->track() == nullptr);
	block->set_track(this);

	// Update array
	int array_index = connect_block(block);
	blocks_.insert(index, block);
	block_array_indexes_.insert(index, array_index);

	// Handle previous/next
	Block *previous = (index > 0) ? blocks_.at(index - 1) : nullptr;
	Block *next = (index < blocks_.size() - 1) ? blocks_.at(index + 1) :
												 nullptr;
	Block::set_previous_next(previous, block);
	Block::set_previous_next(block, next);

	// Update in/out
	update_in_out_from(index);

	connect(block, &Block::length_changed, this, &Track::block_length_changed);

	Node::invalidate_cache(TimeRange(block->in(), track_length()), k_block_input);

	emit block_added(block);

	update_array_map();
}

void Track::append_block(Block *block)
{
	insert_block_at_index(block, blocks_.size());
}

void Track::ripple_remove_block(Block *block)
{
	Rational remove_in = block->in();
	Rational remove_out = block->out();

	emit block_removed(block);

	// Set track
	Q_ASSERT(block->track() == this);
	block->set_track(nullptr);

	// Update array
	int index = blocks_.indexOf(block);
	Q_ASSERT(index != -1);

	int array_index = block_array_indexes_.at(index);

	blocks_.removeAt(index);
	block_array_indexes_.removeAt(index);

	Node::disconnect_edge(block, NodeInput(this, k_block_input, array_index));
	empty_inputs_.push_back(array_index);
	disconnect(block, &Block::length_changed, this, &Track::block_length_changed);

	// Handle previous/next
	Block *previous = (index > 0) ? blocks_.at(index - 1) : nullptr;
	Block *next = (index < blocks_.size()) ? blocks_.at(index) : nullptr;
	Block::set_previous_next(previous, next);
	block->set_previous(nullptr);
	block->set_next(nullptr);
	block->set_in(0);
	block->set_out(block->length());

	// Update in/outs
	update_in_out_from(index);

	Node::invalidate_cache(
		TimeRange(remove_in, qMax(track_length(), remove_out)), k_block_input);

	update_array_map();
}

void Track::replace_block(Block *old, Block *replace)
{
	emit block_removed(old);

	// Set track
	Q_ASSERT(old->track() == this);
	old->set_track(nullptr);

	Q_ASSERT(replace->track() == nullptr);
	replace->set_track(this);

	// Update array
	int cache_index = blocks_.indexOf(old);
	int index_of_old_block = get_array_index_from_cache_index(cache_index);

	ignore_block_disconnect_++;
	disconnect_edge(old, NodeInput(this, k_block_input, index_of_old_block));
	ignore_block_disconnect_--;
	connect_edge(replace, NodeInput(this, k_block_input, index_of_old_block));
	blocks_.replace(cache_index, replace);
	disconnect(old, &Block::length_changed, this, &Track::block_length_changed);
	connect(replace, &Block::length_changed, this, &Track::block_length_changed);

	// Handle previous/next
	replace->set_previous(old->previous());
	replace->set_next(old->next());
	old->set_previous(nullptr);
	old->set_next(nullptr);
	if (replace->previous()) {
		replace->previous()->set_next(replace);
	}
	if (replace->next()) {
		replace->next()->set_previous(replace);
	}

	if (old->length() == replace->length()) {
		replace->set_in(replace->previous() ? replace->previous()->out() : 0);
		replace->set_out(replace->in() + replace->length());

		Node::invalidate_cache(TimeRange(replace->in(), replace->out()),
							  k_block_input);
	} else {
		// Update in/outs
		update_in_out_from(cache_index);

		Node::invalidate_cache(TimeRange(replace->in(), track_length()),
							  k_block_input);
	}

	emit block_added(replace);

	update_array_map();
}

Rational Track::track_length() const
{
	if (blocks_.isEmpty()) {
		return 0;
	} else {
		return blocks_.last()->out();
	}
}

bool Track::is_muted() const
{
	return get_standard_value(k_muted_input).toBool();
}

bool Track::is_locked() const
{
	return locked_;
}

void Track::set_muted(bool e)
{
	set_standard_value(k_muted_input, e);
}

void Track::set_locked(bool e)
{
	locked_ = e;
}

void Track::InputConnectedEvent(const QString &input, int element, Node *node)
{
	if (arraymap_invalid_ && input == k_block_input && element >= 0) {
		refresh_block_cache_from_array_map();
	}
}

void Track::InputDisconnectedEvent(const QString &input, int element,
								   Node *output)
{
	Node::InputDisconnectedEvent(input, element, output);

	// Keep the block cache consistent when a block edge is removed outside
	// the Track's own mutating operations (e.g. the block is being deleted,
	// or an undo command is detaching the whole track from the graph).
	// Without this, blocks_ keeps a dangling pointer and later readers such
	// as track_length() walk into freed memory.
	//
	// Only the volatile cache is trimmed here; the persistent array map is
	// deliberately left alone so that undo can re-attach the blocks from it
	// (InputConnectedEvent rebuilds the cache while arraymap_invalid_ is
	// set).
	if (input == k_block_input && ignore_block_disconnect_ == 0) {
		const int index = blocks_.indexOf(static_cast<Block *>(output));
		if (index != -1) {
			blocks_.removeAt(index);
			block_array_indexes_.removeAt(index);
			arraymap_invalid_ = true;

			Block *previous = (index > 0) ? blocks_.at(index - 1) : nullptr;
			Block *next = (index < blocks_.size()) ? blocks_.at(index) : nullptr;
			Block::set_previous_next(previous, next);

			update_in_out_from(index);
		}
	}
}

void Track::update_in_out_from(int index)
{
	// Find block just before this one to find the last out point
	Rational last_out = (index == 0) ? 0 : blocks_.at(index - 1)->out();

	// Iterate through all blocks updating their in/outs
	for (int i = index; i < blocks_.size(); i++) {
		Block *b = blocks_.at(i);

		b->set_in(last_out);

		last_out += b->length();

		b->set_out(last_out);
	}

	emit blocks_refreshed();

	// Update track length
	emit track_length_changed();
}

int Track::get_array_index_from_block(Block *block) const
{
	return block_array_indexes_.at(blocks_.indexOf(block));
}

int Track::get_array_index_from_cache_index(int index) const
{
	return block_array_indexes_.at(index);
}

int Track::get_cache_index_from_array_index(int index) const
{
	return block_array_indexes_.indexOf(index);
}

int Track::get_block_index_at_time(const Rational &time) const
{
	if (time < 0 || time >= track_length()) {
		return -1;
	}

	// Use binary search to find block at time
	int low = 0;
	int high = blocks_.size() - 1;
	while (low <= high) {
		int mid = low + (high - low) / 2;

		Block *block = blocks_.at(mid);
		if (block->in() <= time && block->out() > time) {
			return mid;
		} else if (block->out() <= time) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	return -1;
}

void Track::process_audio_track(const NodeValueRow &value,
							  const NodeGlobals &globals,
							  NodeValueTable *table) const
{
	const TimeRange &range = globals.time();

	// All these blocks will need to output to a buffer so we create one here
	SampleBuffer block_range_buffer(globals.aparams(), range.length());
	block_range_buffer.silence();

	// Loop through active blocks retrieving their audio
	NodeValueArray arr = value[k_block_input].to_array();

	for (auto it = arr.cbegin(); it != arr.cend(); it++) {
		Block *b = blocks_.at(get_cache_index_from_array_index(it->first));

		TimeRange range_for_block(qMax(b->in(), range.in()),
								  qMin(b->out(), range.out()));

		qint64 source_offset = 0;
		qint64 destination_offset = globals.aparams().time_to_samples(
			range_for_block.in() - range.in());
		qint64 max_dest_sz =
			globals.aparams().time_to_samples(range_for_block.length());

		// Destination buffer
		SampleBuffer samples_from_this_block = it->second.to_samples();

		if (samples_from_this_block.is_allocated()) {
			// If this is a clip, we might have extra speed/reverse information
			if (ClipBlock *clip_cast = dynamic_cast<ClipBlock *>(b)) {
				double speed_value = clip_cast->speed();
				bool reversed = clip_cast->reverse();

				if (qIsNull(speed_value)) {
					// Just silence, don't think there's any other practical application of 0 speed audio
					samples_from_this_block.silence();
				} else if (!qFuzzyCompare(speed_value, 1.0)) {
					if (clip_cast->maintain_audio_pitch()) {
						AudioProcessor processor;

						if (processor.open(
								samples_from_this_block.audio_params(),
								samples_from_this_block.audio_params(),
								speed_value)) {
							AudioProcessor::Buffer out;

							// FIXME: This is not the best way to do this, the TempoProcessor works best
							//        when it's given a continuous stream of audio, which is challenging
							//        in our current "modular" audio system. This should still work reasonably
							//        well on export (assuming audio is all generated at once on export), but
							//        users may hear clicks and pops in the audio during preview due to this
							//        approach.
							int r = processor.convert(
								samples_from_this_block.to_raw_ptrs().data(),
								samples_from_this_block.sample_count(),
								nullptr);

							if (r < 0) {
								qCritical()
									<< "Failed to change tempo of audio:" << r;
							} else {
								processor.flush();

								processor.convert(nullptr, 0, &out);

								if (!out.empty()) {
									int nb_samples =
										out.front().size() *
										samples_from_this_block.audio_params()
											.bytes_per_sample_per_channel();

									if (nb_samples) {
										SampleBuffer new_samples(
											samples_from_this_block
												.audio_params(),
											nb_samples);

										for (int i = 0; i < out.size(); i++) {
											memcpy(new_samples.data(i),
												   out[i].data(),
												   out[i].size());
										}

										samples_from_this_block = new_samples;
									}
								}
							}
						}
					} else {
						// Multiply time
						samples_from_this_block.speed(speed_value);
					}
				}

				if (reversed) {
					samples_from_this_block.reverse();
				}
			}

			qint64 copy_length = qMin(
				max_dest_sz,
				qint64(samples_from_this_block.sample_count() - source_offset));

			// Copy samples into destination buffer
			for (int i = 0;
				 i < samples_from_this_block.audio_params().channel_count();
				 i++) {
				block_range_buffer.set(
					i, samples_from_this_block.data(i) + source_offset,
					destination_offset, copy_length);
			}
		}
	}

	table->push(NodeValue::k_samples, QVariant::fromValue(block_range_buffer),
				this);
}

int Track::connect_block(Block *b)
{
	if (!empty_inputs_.empty()) {
		int index = empty_inputs_.front();
		empty_inputs_.pop_front();

		Node::connect_edge(b, NodeInput(this, k_block_input, index));

		return index;
	} else {
		int old_sz = input_array_size(k_block_input);
		input_array_append(k_block_input);
		Node::connect_edge(b, NodeInput(this, k_block_input, old_sz));
		return old_sz;
	}
}

void Track::update_array_map()
{
	ignore_arraymap_++;
	set_standard_value(
		k_array_map_input,
		QByteArray(reinterpret_cast<const char *>(block_array_indexes_.data()),
				   block_array_indexes_.size() * sizeof(uint32_t)));
}

void Track::refresh_block_cache_from_array_map()
{
	if (ignore_arraymap_set_) {
		return;
	}

	// Disconnecting any existing blocks
	for (Block *b : blocks_) {
		Q_ASSERT(b->track() == this);
		b->set_track(nullptr);
		b->set_previous(nullptr);
		b->set_next(nullptr);
		b->set_in(0);
		b->set_out(b->length());
		disconnect(b, &Block::length_changed, this, &Track::block_length_changed);
	}

	QByteArray bytes = get_standard_value(k_array_map_input).toByteArray();
	block_array_indexes_.resize(bytes.size() / sizeof(uint32_t));
	memcpy(block_array_indexes_.data(), bytes.data(), bytes.size());
	blocks_.clear();
	blocks_.reserve(block_array_indexes_.size());

	Block *prev = nullptr;
	arraymap_invalid_ = false;

	for (int i = 0; i < block_array_indexes_.size(); i++) {
		Block *b = static_cast<Block *>(
			get_connected_output(k_block_input, block_array_indexes_.at(i)));

		Block::set_previous_next(prev, b);

		if (b) {
			b->set_track(this);
			connect(b, &Block::length_changed, this, &Track::block_length_changed);

			blocks_.append(b);
			prev = b;
		} else {
			block_array_indexes_.resize(i);
			arraymap_invalid_ = true;
			break;
		}
	}

	if (prev) {
		prev->set_next(nullptr);
	}

	update_in_out_from(0);
}

void Track::block_length_changed()
{
	// Assumes sender is a Block
	Block *b = static_cast<Block *>(sender());

	update_in_out_from(blocks_.indexOf(b));
}

uint qHash(const Track::Reference &r, uint seed)
{
	// Not super efficient, but couldn't think of any better way to ensure a different hash each time
	return ::qHash(QStringLiteral("%1:%2").arg(QString::number(r.type()),
											   QString::number(r.index())),
				   seed);
}

QDataStream &operator<<(QDataStream &out, const Track::Reference &ref)
{
	out << static_cast<int>(ref.type()) << ref.index();

	return out;
}

QDataStream &operator>>(QDataStream &in, Track::Reference &ref)
{
	int type;
	int index;

	in >> type >> index;

	ref = Track::Reference(static_cast<Track::Type>(type), index);

	return in;
}

}
