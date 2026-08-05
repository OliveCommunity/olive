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

#include <string>
#include <vector>

/**
 * @brief Command-line argument parser
 *
 * You may be wondering why we don't use a library implementation like
 * QCommandLineParser instead of a custom implementation like this. The
 * reason why is because QCommandLineParser requires a QApplication object
 * of some kind to already have been created before it can parse anything,
 * but we need to be able to control whether a QApplication (GUI-mode) or
 * a QCoreApplication (CLI-mode) is created which is set by the user as a
 * command line argument. Therefore we needed a custom implementation that
 * could parse arguments without the need for any application object to be
 * present already.
 */
class CommandLineParser {
public:
	CommandLineParser() = default;
	~CommandLineParser();

	CommandLineParser(const CommandLineParser &) = delete;
	CommandLineParser(CommandLineParser &&) = delete;
	CommandLineParser &operator=(const CommandLineParser &) = delete;
	CommandLineParser &operator=(CommandLineParser &&) = delete;

	class PositionalArgument {
	public:
		PositionalArgument() = default;

		const std::string &get_setting() const
		{
			return setting_;
		}

		void set_setting(const std::string &s)
		{
			setting_ = s;
		}

	private:
		std::string setting_;
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

	const Option *add_option(const std::vector<std::string> &strings,
							 const std::string &description, bool takes_arg = false,
							 const std::string &arg_placeholder = std::string(),
							 bool hidden = false);

	const PositionalArgument *add_positional_argument(const std::string &name,
													const std::string &description,
													bool required = false);

	void process(const std::vector<std::string> &argv);

	void print_help(const char *filename);

	/**
	 * @brief Set the application name/version shown by print_help()
	 *
	 * Replaces the former QCoreApplication::applicationName()/applicationVersion()
	 * dependency. Defaults to "oak" with an empty version.
	 */
	void set_app_info(const std::string &name, const std::string &version);

private:
	struct KnownOption {
		std::vector<std::string> args;
		std::string description;
		Option *option;
		bool takes_arg;
		std::string arg_placeholder;
		bool hidden;
	};

	struct KnownPositionalArgument {
		std::string name;
		std::string description;
		PositionalArgument *option;
		bool required;
	};

	std::vector<KnownOption> options_;

	std::vector<KnownPositionalArgument> positional_args_;

	std::string app_name_ = "oak";

	std::string app_version_;
};

#endif // OAK_COMMANDLINEPARSER_H
