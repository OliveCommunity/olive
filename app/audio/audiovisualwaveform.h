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

#ifndef OAK_SUMSAMPLES_H
#define OAK_SUMSAMPLES_H

#include <olive/core/core.h>
#include <QPainter>
#include <QVector>

namespace olive
{

using namespace core;

/**
 * @brief A buffer of data used to store a visual representation of audio
 *
 * This differs from a SampleBuffer as the data in an AudioVisualWaveform has been reduced
 * significantly and optimized for visual display.
 */
class AudioVisualWaveform {
public:
	AudioVisualWaveform();

	struct SamplePerChannel {
		float min;
		float max;
	};

	using Sample = std::vector<SamplePerChannel>;

	int channel_count() const
	{
		return channels_;
	}

	void set_channel_count(int channels)
	{
		channels_ = channels;
	}

	const Rational &length() const
	{
		return length_;
	}

	/**
   * @brief Writes samples into the visual waveform buffer
   *
   * Starting at `start`, writes samples over anything in the buffer, expanding it if necessary.
   */
	void overwrite_samples(const SampleBuffer &samples, int sample_rate,
						  const Rational &start = 0);

	/**
   * @brief Replaces sums at a certain range in this visual waveform
   *
   * @param sums
   *
   * The sums to write over our current ones with.
   *
   * @param dest
   *
   * Where in this visual waveform these sums should START being written to.
   *
   * @param offset
   *
   * Where in the `sums` parameter this should start reading from. Defaults to 0.
   *
   * @param length
   *
   * Maximum length of `sums` to overwrite with.
   */
	void overwrite_sums(const AudioVisualWaveform &sums, const Rational &dest,
					   const Rational &offset = 0, const Rational &length = 0);

	void overwrite_silence(const Rational &start, const Rational &length);

	void trim_in(Rational length);

	AudioVisualWaveform mid(const Rational &offset) const;
	AudioVisualWaveform mid(const Rational &offset,
							const Rational &length) const;

	void resize(const Rational &length);

	void trim_range(const Rational &in, const Rational &length);

	Sample get_summary_from_time(const Rational &start,
							  const Rational &length) const;

	static Sample sum_samples(const SampleBuffer &samples, size_t start_index,
							 size_t length);

	static Sample re_sum_samples(const SamplePerChannel *samples,
							   size_t nb_samples, int nb_channels);

	static void draw_sample(QPainter *painter, const Sample &sample, int x,
						   int y, int height, bool rectified);

	static void draw_waveform(QPainter *painter, const QRect &rect,
							 const double &scale,
							 const AudioVisualWaveform &samples,
							 const Rational &start_time);

	// Must be a power of 2
	static const Rational k_minimum_sample_rate;
	static const Rational k_maximum_sample_rate;

private:
	void overwrite_samples_from_buffer(const SampleBuffer &samples,
									int sample_rate, const Rational &start,
									double target_rate, Sample &data,
									size_t &start_index,
									size_t &samples_length);

	void overwrite_samples_from_mipmap(const Sample &input,
									double input_sample_rate,
									size_t &input_start, size_t &input_length,
									const Rational &start, double output_rate,
									Sample &output_data);

	size_t time_to_samples(const Rational &time, double sample_rate) const;
	size_t time_to_samples(const double &time, double sample_rate) const;

	std::map<Rational, Sample>::const_iterator
	get_mipmap_for_scale(double scale) const;

	void validate_virtual_start(const Rational &new_start);

	Rational virtual_start_;

	int channels_;

	std::map<Rational, Sample> mipmapped_data_;

	Rational length_;
};

}

Q_DECLARE_METATYPE(olive::AudioVisualWaveform)

#endif // OAK_SUMSAMPLES_H
