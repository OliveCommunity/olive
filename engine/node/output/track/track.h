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

#ifndef OAK_TRACK_H
#define OAK_TRACK_H

#include "node/block/block.h"

namespace olive
{

class Sequence;

/**
 * @brief A time traversal Node for sorting through one channel/track of Blocks
 */
class Track : public Node {
	Q_OBJECT
public:
	enum Type { k_none = -1, k_video, k_audio, k_subtitle, k_count };

	Track();

	NODE_DEFAULT_FUNCTIONS(Track)

	const Track::Type &type() const;
	void set_type(const Track::Type &track_type);

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual ActiveElements
	get_active_elements_at_time(const QString &input,
							const TimeRange &r) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual TimeRange input_time_adjustment(const QString &input, int element,
										  const TimeRange &input_time,
										  bool clamp) const override;

	virtual TimeRange
	output_time_adjustment(const QString &input, int element,
						 const TimeRange &input_time) const override;

	static Rational transform_time_for_block(const Block *block,
										  const Rational &time)
	{
		if (time == RATIONAL_MAX || time == RATIONAL_MIN) {
			return time;
		}

		return time - block->in();
	}

	static TimeRange transform_range_for_block(const Block *block,
											const TimeRange &range)
	{
		return TimeRange(transform_time_for_block(block, range.in()),
						 transform_time_for_block(block, range.out()));
	}

	static Rational transform_time_from_block(const Block *block,
										   const Rational &time)
	{
		if (time == RATIONAL_MAX || time == RATIONAL_MIN) {
			return time;
		}

		return time + block->in();
	}

	static TimeRange transform_range_from_block(const Block *block,
											 const TimeRange &range)
	{
		return TimeRange(transform_time_from_block(block, range.in()),
						 transform_time_from_block(block, range.out()));
	}

	const double &get_track_height() const;
	void set_track_height(const double &height);

	int get_track_height_in_pixels() const
	{
		return internal_height_to_pixel_height(get_track_height());
	}

	void set_track_height_in_pixels(int h)
	{
		set_track_height(pixel_height_to_internal_height(h));
	}

	virtual bool load_custom(QXmlStreamReader *reader,
							SerializedData *data) override;
	virtual void save_custom(QXmlStreamWriter *writer) const override;
	virtual void PostLoadEvent(SerializedData *data) override;

	static int internal_height_to_pixel_height(double h)
	{
		return qRound(h * QFontMetrics(QFont()).height());
	}

	static double pixel_height_to_internal_height(int h)
	{
		return double(h) / double(QFontMetrics(QFont()).height());
	}

	static int get_default_track_height_in_pixels()
	{
		return internal_height_to_pixel_height(k_track_height_default);
	}

	static int get_minimum_track_height_in_pixels()
	{
		return internal_height_to_pixel_height(k_track_height_minimum);
	}

	virtual void retranslate() override;

	class Reference {
	public:
		Reference()
			: type_(k_none)
			, index_(-1)
		{
		}

		Reference(const Track::Type &type, const int &index)
			: type_(type)
			, index_(index)
		{
		}

		const Track::Type &type() const
		{
			return type_;
		}

		const int &index() const
		{
			return index_;
		}

		bool operator==(const Reference &ref) const
		{
			return type_ == ref.type_ && index_ == ref.index_;
		}

		bool operator!=(const Reference &ref) const
		{
			return !(*this == ref);
		}

		bool operator<(const Track::Reference &rhs) const
		{
			if (type_ != rhs.type_) {
				return type_ < rhs.type_;
			}

			return index_ < rhs.index_;
		}

		QString to_string() const
		{
			QString type_string = type_to_string(type_);
			if (type_string.isEmpty()) {
				return QString();
			} else {
				return QStringLiteral("%1:%2").arg(type_string,
												   QString::number(index_));
			}
		}

		/// For IDs that shouldn't change between localizations
		static QString type_to_string(Type type)
		{
			switch (type) {
			case k_video:
				return QStringLiteral("v");
			case k_audio:
				return QStringLiteral("a");
			case k_subtitle:
				return QStringLiteral("s");
			case k_count:
			case k_none:
				break;
			}

			return QString();
		}

		/// For human-facing strings
		static QString type_to_translated_string(Type type)
		{
			switch (type) {
			case k_video:
				return tr("V");
			case k_audio:
				return tr("A");
			case k_subtitle:
				return tr("S");
			case k_count:
			case k_none:
				break;
			}

			return QString();
		}

		static Type type_from_string(const QString &s)
		{
			if (s.size() >= 3) {
				if (s.at(1) == ':') {
					if (s.at(0) == 'v') {
						// Video stream
						return Track::k_video;
					} else if (s.at(0) == 'a') {
						// Audio stream
						return Track::k_audio;
					} else if (s.at(0) == 's') {
						// Subtitle stream
						return Track::k_subtitle;
					}
				}
			}

			return Track::k_none;
		}

		static Reference from_string(const QString &s)
		{
			Reference ref;
			Type parse_type = type_from_string(s);

			if (parse_type != Track::k_none) {
				bool ok;
				int parse_index = s.mid(2).toInt(&ok);

				if (ok) {
					ref.type_ = parse_type;
					ref.index_ = parse_index;
				}
			}

			return ref;
		}

		bool is_valid() const
		{
			return type_ > k_none && type_ < k_count && index_ >= 0;
		}

	private:
		Track::Type type_;

		int index_;
	};

	Reference to_reference() const
	{
		return Reference(type(), index());
	}

	const int &index() const
	{
		return index_;
	}

	void set_index(const int &index);

	/**
   * @brief Returns the block that starts BEFORE (not AT) and ends AFTER (not AT) a time
   *
   * Catches the first block that matches `block.in < time && block.out > time` or nullptr if any
   * block starts/ends precisely at that time or the time exceeds the track length.
   */
	Block *block_containing_time(const Rational &time) const;

	/**
   * @brief Returns the block that starts BEFORE a given time and ends either AFTER or AT that time
   *
   * @return Catches the first block that matches `block.out >= time` or nullptr if this time
   * exceeds the track length.
   */
	Block *nearest_block_before(const Rational &time) const;

	/**
   * @brief Returns the block that starts BEFORE or AT a given time.
   *
   * @return Catches the first block that matches `block.out > time` or nullptr if this time
   * exceeds the track length.
   */
	Block *nearest_block_before_or_at(const Rational &time) const;

	/**
   * @brief Returns the block that starts either AT a given time or the soonest block AFTER
   *
   * @return Catches the first block that matches `block.in >= time` or nullptr if this time
   * exceeds the track length.
   */
	Block *nearest_block_after_or_at(const Rational &time) const;

	/**
   * @brief Returns the block that starts AFTER the given time (but never AT the given time)
   *
   * @return Catches the first block that matches `block.in > time` or nullptr if this time
   * exceeds the track length.
   */
	Block *nearest_block_after(const Rational &time) const;

	/*
   * @brief Returns whether a time range is empty or only has a gap
   */
	bool is_range_free(const TimeRange &range) const;

	const QVector<Block *> &blocks() const
	{
		return blocks_;
	}

	virtual void invalidate_cache(const TimeRange &range, const QString &from,
								 int element,
								 InvalidateCacheOptions options) override;

	Block *visible_block_at_time(const Rational &t) const
	{
		int index = get_block_index_at_time(t);
		return (index == -1) ? nullptr : blocks_.at(index);
	}

	/**
   * @brief Adds Block `block` at the very beginning of the Sequence before all other clips
   */
	void prepend_block(Block *block);

	/**
   * @brief Inserts Block `block` at a specific index (0 is the start of the timeline)
   *
   * If the index == 0, this function does the same as PrependBlock(). If the index >= the current number of blocks,
   * this function is the same as AppendBlock().
   */
	void insert_block_at_index(Block *block, int index);

	/**
   * @brief Inserts Block after another Block
   *
   * Equivalent to calling InsertBlockBetweenBlocks(block, before, before->next())
   */
	void insert_block_after(Block *block, Block *before);

	/**
   * @brief Inserts Block before another Block
   */
	void insert_block_before(Block *block, Block *after);

	/**
   * @brief Adds Block `block` at the very end of the Sequence after all other clips
   */
	void append_block(Block *block);

	/**
   * @brief Removes a Block pushing all subsequent Blocks earlier to take up the space
   */
	void ripple_remove_block(Block *block);

	/**
   * @brief Replaces Block `old` with Block `replace`
   *
   * Both blocks must have equal lengths.
   */
	void replace_block(Block *old, Block *replace);

	Rational track_length() const;

	bool is_muted() const;

	bool is_locked() const;

	int get_array_index_from_block(Block *block) const;

	Sequence *sequence() const
	{
		return sequence_;
	}

	void set_sequence(Sequence *sequence)
	{
		sequence_ = sequence;
	}

	static const double k_track_height_default;
	static const double k_track_height_minimum;
	static const double k_track_height_interval;

	static const QString k_block_input;
	static const QString k_muted_input;
	static const QString k_array_map_input;

public slots:
	void set_muted(bool e);

	void set_locked(bool e);

signals:
	/**
   * @brief Signal emitted when a Block is added to this Track
   */
	void block_added(Block *block);

	/**
   * @brief Signal emitted when a Block is removed from this Track
   */
	void block_removed(Block *block);

	/**
   * @brief Signal emitted when the length of the track has changed
   */
	void track_length_changed();

	/**
   * @brief Signal emitted when the height of the track has changed
   */
	void track_height_changed(qreal virtual_height);

	/**
   * @brief Signal emitted when the muted setting changes
   */
	void muted_changed(bool e);

	/**
   * @brief Signal emitted when the index has changed
   */
	void index_changed(int old, int now);

	/**
   * @brief Emitted when a block changes length and all the subsequent blocks had to update
   */
	void blocks_refreshed();

protected:
	virtual void InputConnectedEvent(const QString &input, int element,
									 Node *node) override;
	virtual void InputDisconnectedEvent(const QString &input, int element,
										Node *output) override;
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

private:
	void update_in_out_from(int index);

	int get_array_index_from_cache_index(int index) const;

	int get_cache_index_from_array_index(int index) const;

	int get_block_index_at_time(const Rational &time) const;

	void process_audio_track(const NodeValueRow &value,
						   const NodeGlobals &globals,
						   NodeValueTable *table) const;

	int connect_block(Block *b);

	void update_array_map();

	TimeRangeList block_length_pending_invalidations_;

	QVector<Block *> blocks_;
	QVector<uint32_t> block_array_indexes_;

	std::list<int> empty_inputs_;

	Track::Type track_type_;

	double track_height_;

	int index_;

	bool locked_;

	Sequence *sequence_;

	int ignore_arraymap_;
	bool arraymap_invalid_;
	bool ignore_arraymap_set_;

	/**
   * @brief Nestable guard suppressing the block-cache maintenance in
   * InputDisconnectedEvent while the Track itself is rewiring block edges
   * (e.g. replace_block), where the cache update is handled explicitly.
   */
	int ignore_block_disconnect_ = 0;

private slots:
	void block_length_changed();

	void refresh_block_cache_from_array_map();
};

uint qHash(const Track::Reference &r, uint seed = 0);

QDataStream &operator<<(QDataStream &out, const Track::Reference &ref);

QDataStream &operator>>(QDataStream &in, Track::Reference &ref);

}

#endif // OAK_TRACK_H
