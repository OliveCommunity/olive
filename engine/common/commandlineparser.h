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

#ifndef OAK_COMMANDLINEPARSER_H
#define OAK_COMMANDLINEPARSER_H

#include <QStringList>
#include <QVector>

#include "common/define.h"

/**
 * @brief Command-line argument parser
 *
 * You may be wondering why we don't use QCommandLineParser instead of a custom implementation like
 * this. The reason why is because QCommandLineParser requires a QApplication object of some kind
 * to already have been created before it can parse anything, but we need to be able to control
 * whether a QApplication (GUI-mode) or a QCoreApplication (CLI-mode) is created which is set by
 * the user as a command line argument. Therefore we needed a custom implementation that could
 * parse arguments without the need for a Q(Core)Application to be present already.
 */
class CommandLineParser {
public:
	~CommandLineParser();

	DISABLE_COPY_MOVE(CommandLineParser)

	class PositionalArgument {
	public:
		PositionalArgument() = default;

		const QString &get_setting() const
		{
			return setting_;
		}

		void set_setting(const QString &s)
		{
			setting_ = s;
		}

	private:
		QString setting_;
	};

	class Option : public PositionalArgument {
	public:
		Option()
		{
			is_set_ = false;
		}

		bool is_set() const
		{
			return is_set_;
		}

		void set()
		{
			is_set_ = true;
		}

	private:
		bool is_set_;
	};

	CommandLineParser() = default;

	const Option *add_option(const QStringList &strings,
							const QString &description, bool takes_arg = false,
							const QString &arg_placeholder = QString(),
							bool hidden = false);

	const PositionalArgument *add_positional_argument(const QString &name,
													const QString &description,
													bool required = false);

	void process(const QVector<QString> &argv);

	void print_help(const char *filename);

private:
	struct KnownOption {
		QStringList args;
		QString description;
		Option *option;
		bool takes_arg;
		QString arg_placeholder;
		bool hidden;
	};

	struct KnownPositionalArgument {
		QString name;
		QString description;
		PositionalArgument *option;
		bool required;
	};

	QVector<KnownOption> options_;

	QVector<KnownPositionalArgument> positional_args_;
};

#endif // OAK_COMMANDLINEPARSER_H
