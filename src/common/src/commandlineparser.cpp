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

#include "commandlineparser.h"

#include <cctype>
#include <cstdio>
#include <cstring>

CommandLineParser::~CommandLineParser()
{
	for (const KnownOption &o : options_) {
		delete o.option;
	}

	for (const KnownPositionalArgument &a : positional_args_) {
		delete a.option;
	}
}

const CommandLineParser::Option *
CommandLineParser::add_option(const std::vector<std::string> &strings,
							 const std::string &description, bool takes_arg,
							 const std::string &arg_placeholder, bool hidden)
{
	Option *o = new Option();

	options_.push_back(
		{ strings, description, o, takes_arg, arg_placeholder, hidden });

	return o;
}

const CommandLineParser::PositionalArgument *
CommandLineParser::add_positional_argument(const std::string &name,
										 const std::string &description,
										 bool required)
{
	PositionalArgument *a = new PositionalArgument();

	positional_args_.push_back({ name, description, a, required });

	return a;
}

static bool string_equals_case_insensitive(const std::string &a,
										   const std::string &b)
{
	if (a.size() != b.size()) {
		return false;
	}

	for (size_t i = 0; i < a.size(); i++) {
		if (tolower(static_cast<unsigned char>(a[i])) !=
			tolower(static_cast<unsigned char>(b[i]))) {
			return false;
		}
	}

	return true;
}

void CommandLineParser::process(const std::vector<std::string> &argv)
{
	size_t positional_index = 0;

	for (size_t i = 1; i < argv.size(); i++) {
		if (!argv[i].empty() && argv[i][0] == '-') {
			// Must be an option

			// Skip past first dash
			std::string arg_basename = argv[i].substr(1);

			bool matched_known = false;

			for (KnownOption &o : options_) {
				for (const std::string &s : o.args) {
					if (string_equals_case_insensitive(s, arg_basename)) {
						// Flag discovered!
						o.option->set();

						if (o.takes_arg && i + 1 < argv.size()) {
							o.option->set_setting(argv[i + 1]);
							i++;
						}

						matched_known = true;
						goto found_flag;
					}
				}
			}

found_flag:
			if (!matched_known) {
				fprintf(stderr, "Unknown parameter: %s\n", argv[i].c_str());
			}

		} else {
			// Must be a positional flag
			if (positional_index < positional_args_.size()) {
				positional_args_[positional_index].option->set_setting(argv[i]);
				positional_index++;
			} else {
				fprintf(stderr, "Unknown parameter: %s\n", argv[i].c_str());
			}
		}
	}
}

void CommandLineParser::print_help(const char *filename)
{
	printf("%s %s\n", app_name_.c_str(), app_version_.c_str());

	printf("Copyright (C) 2018-2022 Oak Video Editor Team\n");

	std::string positional_args;
	for (size_t i = 0; i < positional_args_.size(); i++) {
		if (i > 0) {
			positional_args.append(" ");
		}

		positional_args.append("[");
		positional_args.append(positional_args_[i].name);
		positional_args.append("]");
	}

	const char *basename;
#ifdef _WIN32
	basename = strrchr(filename, '\\');
	if (!basename) {
		basename = strrchr(filename, '/');
	}
#else
	basename = strrchr(filename, '/');
#endif

	if (basename) {
		// Slash found, increment pointer to avoid showing the slash itself
		basename++;
	} else {
		// If no slashes are found, assume string is already a basename
		basename = filename;
	}

	printf("Usage: %s [options] %s\n\n", basename, positional_args.c_str());
	for (const KnownOption &o : options_) {
		if (o.hidden) {
			continue;
		}

		std::string all_args;

		for (size_t i = 0; i < o.args.size(); i++) {
			if (i > 0) {
				all_args.append(", ");
			}

			all_args.append("-");
			all_args.append(o.args[i]);
		}

		if (o.arg_placeholder.empty()) {
			printf("    %s\n", all_args.c_str());
		} else {
			printf("    %s <%s>\n", all_args.c_str(), o.arg_placeholder.c_str());
		}

		printf("        %s\n\n", o.description.c_str());
	}

	printf("\n");
}

void CommandLineParser::set_app_info(const std::string &name,
									 const std::string &version)
{
	app_name_ = name;
	app_version_ = version;
}
