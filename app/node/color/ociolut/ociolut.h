/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OCIOLUTNODE_H
#define OCIOLUTNODE_H

#include <QMutex>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include "node/color/ociobase/ociobase.h"
#include "render/colorprocessor.h"

namespace olive
{

class OCIOLutNode : public OCIOBaseNode {
	Q_OBJECT
public:
	OCIOLutNode();

	NODE_DEFAULT_FUNCTIONS(OCIOLutNode)

	virtual QString Name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> Category() const override;
	virtual QString Description() const override;

	virtual void Retranslate() override;
	virtual void InputValueChangedEvent(const QString &input,
									int element) override;

	static const QString kFileInput;
	static const QString kDirectionInput;

protected slots:
	virtual void ConfigChanged() override;

private slots:
	void SetProcessorResult(olive::ColorProcessorPtr processor,
							const QString &path, int direction, int generation);

private:
	class GenerateProcessorTask : public QRunnable {
	public:
		GenerateProcessorTask(OCIOLutNode *node, const QString &path,
							  int direction, int generation,
							  ColorManager *manager);

		void run() override;

	private:
		static ColorProcessorPtr CreateProcessor(const QString &path,
											 int direction,
											 ColorManager *manager);

		QPointer<OCIOLutNode> node_;
		QString path_;
		int direction_;
		int generation_;
		ColorManager *manager_;
	};

	void GenerateProcessor();

	QMutex gen_mutex_;
	QString last_path_;
	int last_direction_ = -1;
	ColorProcessorPtr last_processor_;

	QString pending_path_;
	int pending_direction_ = -1;
	int pending_generation_ = 0;
};

} // namespace olive

#endif // OCIOLUTNODE_H
