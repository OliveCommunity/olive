/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "common/commandlineparser.h"

#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "../src/commandlineparser.h"

struct OakCommonCommandLineParser {
	CommandLineParser impl;
};

struct OakCommonCommandLineOption {
	CommandLineParser::Option *option;
};

struct OakCommonCommandLinePositionalArgument {
	CommandLineParser::PositionalArgument *argument;
};

OakCommonCommandLineParser *oakcommon_commandlineparser_init(void)
{
	try {
		return new (std::nothrow) OakCommonCommandLineParser();
	} catch (...) {
		return NULL;
	}
}

void oakcommon_commandlineparser_free(OakCommonCommandLineParser *parser)
{
	delete parser;
}

int oakcommon_commandlineparser_set_app_info(OakCommonCommandLineParser *parser,
											 const char *name,
											 const char *version)
{
	if (!parser || !name) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		parser->impl.set_app_info(name, version ? version : "");
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_add_option(
	OakCommonCommandLineParser *parser, const char *const *names, int name_count,
	const char *description, int takes_arg, const char *arg_placeholder,
	int hidden, OakCommonCommandLineOption **out_option)
{
	if (!parser || !names || name_count <= 0) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		std::vector<std::string> strings;
		strings.reserve(static_cast<size_t>(name_count));
		for (int i = 0; i < name_count; i++) {
			if (!names[i]) {
				return OAKCOMMON_E_INVALID;
			}
			strings.emplace_back(names[i]);
		}

		const CommandLineParser::Option *option = parser->impl.add_option(
			strings, description ? description : "", takes_arg != 0,
			arg_placeholder ? arg_placeholder : "", hidden != 0);

		if (out_option) {
			auto *handle =
				new (std::nothrow) OakCommonCommandLineOption();
			if (!handle) {
				return OAKCOMMON_E_NOMEM;
			}
			handle->option = const_cast<CommandLineParser::Option *>(option);
			*out_option = handle;
		}

		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_add_positional_argument(
	OakCommonCommandLineParser *parser, const char *name,
	const char *description, int required,
	OakCommonCommandLinePositionalArgument **out_argument)
{
	if (!parser || !name) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		const CommandLineParser::PositionalArgument *argument =
			parser->impl.add_positional_argument(
				name, description ? description : "", required != 0);

		if (out_argument) {
			auto *handle =
				new (std::nothrow) OakCommonCommandLinePositionalArgument();
			if (!handle) {
				return OAKCOMMON_E_NOMEM;
			}
			handle->argument =
				const_cast<CommandLineParser::PositionalArgument *>(argument);
			*out_argument = handle;
		}

		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_process(OakCommonCommandLineParser *parser,
										const char *const *argv, int argc)
{
	if (!parser || !argv || argc < 0) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		std::vector<std::string> args;
		args.reserve(static_cast<size_t>(argc));
		for (int i = 0; i < argc; i++) {
			args.emplace_back(argv[i] ? argv[i] : "");
		}

		parser->impl.process(args);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_print_help(OakCommonCommandLineParser *parser,
										   const char *filename)
{
	if (!parser || !filename) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		parser->impl.print_help(filename);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineoption_is_set(OakCommonCommandLineOption *option,
									   bool *is_set)
{
	if (!option || !option->option || !is_set) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		*is_set = option->option->is_set();
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

/**
 * @brief Shared two-stage string getter.
 *
 * Returns the required buffer size in bytes (including the terminating
 * NUL) as a non-negative value, or a negative error code.
 */
static int copy_setting(const std::string &value, char *buf, int buf_size)
{
	int required = static_cast<int>(value.size()) + 1;

	if (buf && buf_size > 0) {
		size_t copy_len = value.size();
		if (copy_len > static_cast<size_t>(buf_size) - 1) {
			copy_len = static_cast<size_t>(buf_size) - 1;
		}
		memcpy(buf, value.data(), copy_len);
		buf[copy_len] = '\0';
	}

	return required;
}

int oakcommon_commandlineoption_get_setting(OakCommonCommandLineOption *option,
											char *buf, int buf_size)
{
	if (!option || !option->option) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return copy_setting(option->option->get_setting(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineoption_set_setting(OakCommonCommandLineOption *option,
											const char *value)
{
	if (!option || !option->option || !value) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		option->option->set_setting(value);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlinepositionalargument_get_setting(
	OakCommonCommandLinePositionalArgument *argument, char *buf, int buf_size)
{
	if (!argument || !argument->argument) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return copy_setting(argument->argument->get_setting(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlinepositionalargument_set_setting(
	OakCommonCommandLinePositionalArgument *argument, const char *value)
{
	if (!argument || !argument->argument || !value) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		argument->argument->set_setting(value);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

void oakcommon_commandlineoption_free(OakCommonCommandLineOption *option)
{
	delete option;
}

void oakcommon_commandlinepositionalargument_free(
	OakCommonCommandLinePositionalArgument *argument)
{
	delete argument;
}
