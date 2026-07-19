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

#ifndef OAK_PRESETMANAGER_H
#define OAK_PRESETMANAGER_H

#include <memory>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QObject>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "common/define.h"
#include "common/filefunctions.h"
#include "common/xmlutils.h"

namespace olive
{

class Preset {
public:
	Preset() = default;

	virtual ~Preset()
	{
	}

	const QString &get_name() const
	{
		return name_;
	}

	void set_name(const QString &s)
	{
		name_ = s;
	}

	virtual void load(QXmlStreamReader *reader) = 0;

	virtual void save(QXmlStreamWriter *writer) const = 0;

private:
	QString name_;
};

using PresetPtr = std::shared_ptr<Preset>;

template <typename T> class PresetManager {
public:
	PresetManager(QWidget *parent, const QString &preset_name)
		: preset_name_(preset_name)
		, parent_(parent)
	{
		// Load custom preset data from file
		QFile preset_file(get_custom_preset_filename());
		if (preset_file.open(QFile::ReadOnly)) {
			QXmlStreamReader reader(&preset_file);

			while (xml_read_next_start_element(&reader)) {
				if (reader.name() == QStringLiteral("presets")) {
					while (xml_read_next_start_element(&reader)) {
						if (reader.name() == QStringLiteral("preset")) {
							PresetPtr p = std::make_unique<T>();

							p->load(&reader);

							custom_preset_data_.append(p);
						} else {
							reader.skipCurrentElement();
						}
					}
				} else {
					reader.skipCurrentElement();
				}
			}

			preset_file.close();
		}
	}

	~PresetManager()
	{
		// Save custom presets to disk
		QFile preset_file(get_custom_preset_filename());
		if (preset_file.open(QFile::WriteOnly)) {
			QXmlStreamWriter writer(&preset_file);
			writer.setAutoFormatting(true);

			writer.writeStartDocument();

			writer.writeStartElement(QStringLiteral("presets"));

			foreach (PresetPtr p, custom_preset_data_) {
				writer.writeStartElement(QStringLiteral("preset"));

				p->save(&writer);

				writer.writeEndElement(); // preset
			}

			writer.writeEndElement(); // presets

			writer.writeEndDocument();

			preset_file.close();
		}
	}

	QString get_preset_name(QString start) const
	{
		bool ok;

		forever
		{
			start = QInputDialog::getText(
				parent_,
				QCoreApplication::translate("PresetManager", "Save Preset"),
				QCoreApplication::translate("PresetManager",
											"Set preset name:"),
				QLineEdit::Normal, start, &ok);

			if (!ok) {
				// Dialog cancelled - leave function entirely
				return QString();
			}

			if (start.isEmpty()) {
				// No preset name entered, start loop over
				QMessageBox::critical(
					parent_,
					QCoreApplication::translate("PresetManager",
												"Invalid preset name"),
					QCoreApplication::translate("PresetManager",
												"You must enter a preset name"),
					QMessageBox::Ok);
			} else {
				break;
			}
		}

		return start;
	}

	enum SaveStatus { k_appended, k_replaced, k_not_saved };

	SaveStatus save_preset(PresetPtr preset)
	{
		QString preset_name;
		int existing_preset;

		forever
		{
			preset_name = get_preset_name(preset_name);

			if (preset_name.isEmpty()) {
				// Dialog cancelled - leave function entirely
				return k_not_saved;
			}

			existing_preset = -1;
			for (int i = 0; i < custom_preset_data_.size(); i++) {
				if (custom_preset_data_.at(i)->get_name() == preset_name) {
					existing_preset = i;
					break;
				}
			}

			if (existing_preset == -1 ||
				QMessageBox::question(
					parent_,
					QCoreApplication::translate("PresetManager",
												"Preset exists"),
					QCoreApplication::translate(
						"PresetManager",
						"A preset with this name already exists. "
						"Would you like to replace it?")) == QMessageBox::Yes) {
				break;
			}
		}

		preset->set_name(preset_name);

		if (existing_preset >= 0) {
			custom_preset_data_.replace(existing_preset, preset);
			return k_replaced;
		} else {
			custom_preset_data_.append(preset);
			return k_appended;
		}
	}

	QString get_custom_preset_filename() const
	{
		return QDir(FileFunctions::get_configuration_location())
			.filePath(preset_name_);
	}

	PresetPtr get_preset(int index)
	{
		return custom_preset_data_.at(index);
	}

	void delete_preset(int index)
	{
		custom_preset_data_.removeAt(index);
	}

	int get_number_of_presets() const
	{
		return custom_preset_data_.size();
	}

	const QVector<PresetPtr> &get_preset_data() const
	{
		return custom_preset_data_;
	}

private:
	QVector<PresetPtr> custom_preset_data_;

	QString preset_name_;

	QWidget *parent_;
};

}

#endif // OAK_PRESETMANAGER_H
