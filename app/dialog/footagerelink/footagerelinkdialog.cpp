/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2019  Olive Team
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

#include "footagerelinkdialog.h"

#include "core.h"
#include "oakengine/footage.h"
#include "oakengine/node.h"
#include "oakengine/project.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace olive
{

FootageRelinkDialog::FootageRelinkDialog(const QVector<Footage *> &footage,
										 QWidget *parent)
	: QDialog(parent)
	, footage_(footage)
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	layout->addWidget(new QLabel(
		"The following files couldn't be found. Clips using them will be "
		"unplayable until they're relinked."));

	table_ = new QTreeWidget();

	table_->setColumnCount(3);
	table_->setHeaderLabels({ tr("Footage"), tr("Filename"), tr("Actions") });
	table_->setRootIsDecorated(false);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->header()->setSectionsMovable(false);

	// Prefer stretching URL column (QHeaderView defaults to stretching the last column, which in
	// our case is just a browse button)
	table_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	table_->header()->setStretchLastSection(false);

	for (int i = 0; i < footage.size(); i++) {
		Footage *f = footage.at(i);
		QTreeWidgetItem *item = new QTreeWidgetItem();

		QWidget *item_actions = new QWidget();
		QHBoxLayout *item_actions_layout = new QHBoxLayout(item_actions);
		QPushButton *item_browse_btn = new QPushButton(tr("Browse"));
		item_browse_btn->setProperty("index", i);
		connect(item_browse_btn, &QPushButton::clicked, this,
				&FootageRelinkDialog::browse_for_footage);
		item_actions_layout->addWidget(item_browse_btn);

		item->setIcon(0, f->data(Node::icon).value<QIcon>());
		item->setText(0, f->get_label());
		item->setText(1, f->filename());

		table_->addTopLevelItem(item);

		table_->setItemWidget(item, 2, item_actions);
	}

	layout->addWidget(table_);

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this,
			&FootageRelinkDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this,
			&FootageRelinkDialog::reject);
	layout->addWidget(buttons);

	setWindowTitle(tr("Relink Footage"));
}

void FootageRelinkDialog::update_footage_item(int index)
{
	Footage *f = footage_.at(index);
	QTreeWidgetItem *item = table_->topLevelItem(index);
	item->setIcon(0, f->data(Node::icon).value<QIcon>());
	item->setText(1, f->filename());
}

void FootageRelinkDialog::browse_for_footage()
{
	int index = sender()->property("index").toInt();
	Footage *f = footage_.at(index);

	QFileInfo info(f->filename());

	QString new_fn = QFileDialog::getOpenFileName(
		this, tr("Relink \"%1\"").arg(f->get_label()), info.absolutePath(),
		Core::footage_file_dialog_filter());

	// Originally, this function would attempt to filter to the exact filename of the missing file.
	// However, this would break on Windows if the filename had any spaces in it. The reason is
	// Windows separates its extensions with ';' while Qt separates them with ' '. Qt isn't
	// intelligent enough to determine whether it's a list of extensions or a single filename with a
	// space in it, it just does a global replace of ' ' to ';'. There's no way around it, outside of
	// bypassing Qt entirely and using Win32's GetOpenFileName() directly. As annoying as it is, I've
	// just disabled it for now.
	//QStringLiteral("%1 (\"%1\");;%2 (*)").arg(info.fileName(), tr("All Files")));

	// We received a new filename
	if (!new_fn.isEmpty()) {
		if (!Core::is_footage_extension_allowed(new_fn)) {
			QMessageBox::warning(
				this, tr("Unsupported media"),
				tr("This file type is not allowed by the current media type "
				   "filter."));
			return;
		}
		// Store original dir since we might be able to use this to find other files
		QDir original_dir = info.dir();
		QDir new_dir = QFileInfo(new_fn).dir();

		// Relink through the facade (reprobes the file and resets stream /
		// proxy state; relinked footage becomes valid when the probe
		// succeeds).
		OakEngineFootage *relink_handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(f));
		const int relink_rc = oakengine_footage_relink(
			relink_handle, new_fn.toUtf8().constData());
		oakengine_footage_free(relink_handle);
		if (relink_rc != OAKENGINE_OK) {
			char err[512];
			err[0] = '\0';
			oakengine_footage_last_error(err, sizeof(err));
			QMessageBox::warning(this, tr("Cannot relink footage"),
								 err[0] ? QString::fromUtf8(err) :
										  tr("The file could not be used as media."));
			return;
		}

		// Update item visually
		update_footage_item(index);

		// Check all other footage files for matches in the new directory
		// (facade's exact file-name matching, mirroring the second attempt
		// of the old per-footage loop).
		Project *project = f->project();
		if (project) {
			oakengine_project_find_offline_footage(
				reinterpret_cast<OakEngineProject *>(project),
				new_dir.absolutePath().toUtf8().constData());

			// The old dialog also tried the original directory's relative
			// paths, which the facade's exact-name matching does not cover;
			// keep that pass here.
			for (int it = 0; it < footage_.size(); it++) {
				Footage *other_footage = footage_.at(it);

				// Ignore footage that's already valid of course
				if (!other_footage->is_valid()) {
					// Get footage path relative to original directory
					QString relative_to_original =
						original_dir.relativeFilePath(other_footage->filename());
					QString absolute_to_new =
						new_dir.filePath(relative_to_original);

					if (QFileInfo::exists(absolute_to_new)) {
						oakengine_footage_relink(
							reinterpret_cast<OakEngineFootage *>(other_footage),
							absolute_to_new.toUtf8().constData());
					}
				}
			}

			// Refresh every row whose validity may have changed.
			for (int it = 0; it < footage_.size(); it++) {
				update_footage_item(it);
			}
		}
	}

	// Check where the next invalid footage is. If there is none, accept automatically. Otherwise,
	// jump to that footage so the user knows where it is.
	int next_invalid = -1;
	for (int i = 0; i < footage_.size(); i++) {
		if (!footage_.at(i)->is_valid()) {
			next_invalid = i;
			break;
		}
	}

	if (next_invalid == -1) {
		// No more invalid footage, just accept
		this->accept();
	} else {
		// Jump to next invalid footage
		QModelIndex idx = table_->model()->index(next_invalid, 0);
		table_->selectionModel()->select(idx, QItemSelectionModel::Select |
												  QItemSelectionModel::Rows);
		table_->scrollTo(idx);
	}
}

}
