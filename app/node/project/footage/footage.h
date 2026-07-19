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

#ifndef OAK_FOOTAGE_H
#define OAK_FOOTAGE_H

#include <olive/core/core.h>
#include <QList>
#include <QDateTime>

#include "codec/decoder.h"
#include "codec/proxymanager.h"
#include "footagedescription.h"
#include "node/output/viewer/viewer.h"
#include "render/cancelatom.h"
#include "render/videoparams.h"

namespace olive
{

/**
 * @brief A reference to an external media file with metadata in a project structure
 *
 * Footage objects serve two purposes: storing metadata about external media and storing it as a project item.
 * Footage objects store a list of Stream objects which store the majority of video/audio metadata. These streams
 * are identical to the stream data in the files.
 */
class Footage : public ViewerOutput {
	Q_OBJECT
public:
	/**
   * @brief Footage Constructor
   */
	Footage(const QString &filename = QString());

	NODE_DEFAULT_FUNCTIONS(Footage)

	virtual QString name() const override
	{
		return tr("Media");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.Olive.footage");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_project };
	}

	virtual QString description() const override
	{
		return tr(
			"Import video, audio, or still image files into the composition.");
	}

	virtual void retranslate() override;

	/**
   * @brief Reset Footage state ready for running through Probe() again
   *
   * If a Footage object needs to be re-probed (e.g. source file changes or Footage is linked to a new file), its
   * state needs to be reset so the Decoder::Probe() function can accurately mirror the source file. Clear() will
   * reset the Footage object's state to being freshly created (keeping the filename).
   *
   * In most cases, you'll be using olive::ProbeMedia() for re-probing which already runs Clear(), so you won't need
   * to worry about this.
   */
	void clear();

	bool is_valid() const
	{
		return valid_;
	}

	/**
   * @brief Sets this footage to valid and ready to use
   */
	void set_valid();

	/**
   * @brief Return the current filename of this Footage object
   */
	QString filename() const;

	/**
   * @brief Set the filename
   *
   * NOTE: This does not automtaically clear the old streams and re-probe for new ones. If the file link has been
   * changed, this will need to be done manually.
   *
   * @param s
   *
   * New filename
   */
	void set_filename(const QString &s);

	/**
   * @brief Retrieve the last modified time/date
   *
   * The file's last modified timestamp is stored for potential organization in the ProjectExplorer. It can be
   * retrieved here.
   */
	const qint64 &timestamp() const;

	/**
   * @brief Set the last modified time/date
   *
   * This should probably only be done on import or replace.
   *
   * @param t
   *
   * New last modified time/date
   */
	void set_timestamp(const qint64 &t);

	void set_cancel_pointer(CancelAtom *c)
	{
		cancelled_ = c;
	}

	int get_stream_index(Track::Type type, int index) const;
	int get_stream_index(const Track::Reference &ref) const
	{
		return get_stream_index(ref.type(), ref.index());
	}

	Track::Reference get_reference_from_real_index(int real_index) const;

	/**
   * @brief Get the Decoder ID set when this Footage was probed
   *
   * @return
   *
   * A decoder ID
   */
	const QString &decoder() const;

	bool has_source_start_time() const
	{
		return has_source_start_time_;
	}

	const Rational &source_start_time() const
	{
		return source_start_time_;
	}

	const QString &source_start_time_source() const
	{
		return source_start_time_source_;
	}

	void set_source_start_time(const Rational &time, const QString &source);

	/**
	 * @brief Removes any source start time (auto-detected or manual)
	 */
	void clear_source_start_time();

	bool proxy_enabled() const
	{
		return proxy_enabled_;
	}

	void set_proxy_enabled(bool enabled);

	const QString &proxy_path() const
	{
		return proxy_path_;
	}

	int proxy_video_stream_index() const
	{
		return proxy_video_stream_index_;
	}

	int proxy_preset_version() const
	{
		return proxy_preset_version_;
	}

	ProxyManager::ProxyState proxy_state() const
	{
		return proxy_state_;
	}

	void set_proxy(const QString &path, ProxyManager::ProxyState state,
				  int video_stream_index, int preset_version, bool enabled);

	void clear_proxy();

	/**
	 * @brief Returns true if this footage uses its own proxy parameters
	 * instead of the global proxy settings
	 */
	bool has_custom_proxy_params() const
	{
		return has_custom_proxy_params_;
	}

	const ProxyManager::ProxyParams &custom_proxy_params() const
	{
		return custom_proxy_params_;
	}

	/**
	 * @brief Sets per-footage proxy parameters, overriding the global settings
	 */
	void set_custom_proxy_params(const ProxyManager::ProxyParams &params);

	/**
	 * @brief Reverts this footage to using the global proxy settings
	 */
	void clear_custom_proxy_params();

	/**
	 * @brief Returns the custom proxy parameters if set, otherwise the
	 * parameters from the global application config
	 */
	ProxyManager::ProxyParams get_effective_proxy_params() const;

	static QString describe_video_stream(const VideoParams &params);
	static QString describe_audio_stream(const AudioParams &params);
	static QString describe_subtitle_stream(const SubtitleParams &params);

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static QString get_stream_type_name(Track::Type type);

	virtual Node *get_connected_texture_output() override;

	virtual Node *get_connected_sample_output() override;

	static Rational adjust_time_by_loop_mode(Rational time, LoopMode loop_mode,
										 const Rational &length,
										 VideoParams::Type type,
										 const Rational &timebase);

	virtual QVariant data(const DataType &d) const override;

	virtual int get_total_stream_count() const override
	{
		return total_stream_count_;
	}

	virtual bool load_custom(QXmlStreamReader *reader,
							SerializedData *data) override;
	virtual void save_custom(QXmlStreamWriter *writer) const override;

signals:
	void proxy_settings_changed();

public:
	static const QString k_filename_input;

	virtual void AddedToGraphEvent(Project *p) override;
	virtual void RemovedFromGraphEvent(Project *p) override;

protected:
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

	virtual Rational verify_length_internal(Track::Type type) const override;

private:
	QString get_colorspace_to_use(const VideoParams &params) const;

	void reprobe();

	VideoParams merge_video_stream(const VideoParams &base,
								 const VideoParams &over);

	/**
   * @brief Internal timestamp object
   */
	qint64 timestamp_;

	/**
   * @brief Internal attached decoder ID
   */
	QString decoder_;

	Rational source_start_time_;

	QString source_start_time_source_;

	bool has_source_start_time_;

	bool proxy_enabled_;

	QString proxy_path_;

	ProxyManager::ProxyState proxy_state_;

	int proxy_video_stream_index_;

	int proxy_preset_version_;

	bool has_custom_proxy_params_;

	ProxyManager::ProxyParams custom_proxy_params_;

	bool valid_;

	CancelAtom *cancelled_;

	int total_stream_count_;

private slots:
	void check_footage();

	void default_color_space_changed();

	void proxy_ready(const QString &source_filename, int stream_index,
					const QString &proxy_filename);
	void proxy_finished(const QString &source_filename, int stream_index,
					   const QString &proxy_filename,
					   ProxyManager::ProxyState state);
};

}

#endif // OAK_FOOTAGE_H
