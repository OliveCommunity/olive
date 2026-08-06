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
#include "refcounted.h"

namespace
{

/**
 * @brief State boxed behind an option handle's ctx pointer.
 *
 * The option pointer is borrowed: the option itself is owned by the
 * parser, so releasing the box never destroys it.
 */
struct OptionState {
	CommandLineParser::Option *option;
};

/**
 * @brief State boxed behind a positional-argument handle's ctx pointer.
 *
 * The argument pointer is borrowed: the argument itself is owned by the
 * parser, so releasing the box never destroys it.
 */
struct PositionalArgumentState {
	CommandLineParser::PositionalArgument *argument;
};

CommandLineParser *clp(OakCommandLineParser parser)
{
	return oakcommon::handle_impl<CommandLineParser>(parser.ctx);
}

CommandLineParser::Option *clo(OakCommandLineOption option)
{
	OptionState *state =
		oakcommon::handle_impl<OptionState>(option.ctx);
	return state ? state->option : nullptr;
}

CommandLineParser::PositionalArgument *clpa(
	OakCommandLinePositionalArgument argument)
{
	PositionalArgumentState *state =
		oakcommon::handle_impl<PositionalArgumentState>(argument.ctx);
	return state ? state->argument : nullptr;
}

} // namespace

OakCommandLineParser oakcommon_commandlineparser_init(void)
{
	try {
		return oakcommon::make_handle_in_place<OakCommandLineParser,
											   CommandLineParser>();
	} catch (...) {
		OakCommandLineParser h = {};
		return h;
	}
}

void oakcommon_commandlineparser_free(OakCommandLineParser *parser)
{
	oakcommon::free_handle(parser);
}

int oakcommon_commandlineparser_set_app_info(OakCommandLineParser parser,
											 const char *name,
											 const char *version)
{
	if (!clp(parser) || !name) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		clp(parser)->set_app_info(name, version ? version : "");
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_add_option(
	OakCommandLineParser parser, const char *const *names, int name_count,
	const char *description, int takes_arg, const char *arg_placeholder,
	int hidden, OakCommandLineOption *out_option)
{
	if (!clp(parser) || !names || name_count <= 0) {
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

		const CommandLineParser::Option *option = clp(parser)->add_option(
			strings, description ? description : "", takes_arg != 0,
			arg_placeholder ? arg_placeholder : "", hidden != 0);

		if (out_option) {
			*out_option = oakcommon::make_handle<OakCommandLineOption>(
				OptionState{const_cast<CommandLineParser::Option *>(option)});
			if (!out_option->ctx) {
				return OAKCOMMON_E_NOMEM;
			}
		}

		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_add_positional_argument(
	OakCommandLineParser parser, const char *name,
	const char *description, int required,
	OakCommandLinePositionalArgument *out_argument)
{
	if (!clp(parser) || !name) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		const CommandLineParser::PositionalArgument *argument =
			clp(parser)->add_positional_argument(
				name, description ? description : "", required != 0);

		if (out_argument) {
			*out_argument =
				oakcommon::make_handle<OakCommandLinePositionalArgument>(
					PositionalArgumentState{
						const_cast<CommandLineParser::PositionalArgument *>(
							argument)});
			if (!out_argument->ctx) {
				return OAKCOMMON_E_NOMEM;
			}
		}

		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_process(OakCommandLineParser parser,
										const char *const *argv, int argc)
{
	if (!clp(parser) || !argv || argc < 0) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		std::vector<std::string> args;
		args.reserve(static_cast<size_t>(argc));
		for (int i = 0; i < argc; i++) {
			args.emplace_back(argv[i] ? argv[i] : "");
		}

		clp(parser)->process(args);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineparser_print_help(OakCommandLineParser parser,
										   const char *filename)
{
	if (!clp(parser) || !filename) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		clp(parser)->print_help(filename);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineoption_is_set(OakCommandLineOption option,
									   bool *is_set)
{
	if (!clo(option) || !is_set) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		*is_set = clo(option)->is_set();
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

int oakcommon_commandlineoption_get_setting(OakCommandLineOption option,
											char *buf, int buf_size)
{
	if (!clo(option)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return copy_setting(clo(option)->get_setting(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlineoption_set_setting(OakCommandLineOption option,
											const char *value)
{
	if (!clo(option) || !value) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		clo(option)->set_setting(value);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlinepositionalargument_get_setting(
	OakCommandLinePositionalArgument argument, char *buf, int buf_size)
{
	if (!clpa(argument)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return copy_setting(clpa(argument)->get_setting(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_commandlinepositionalargument_set_setting(
	OakCommandLinePositionalArgument argument, const char *value)
{
	if (!clpa(argument) || !value) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		clpa(argument)->set_setting(value);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

void oakcommon_commandlineoption_free(OakCommandLineOption *option)
{
	oakcommon::free_handle(option);
}

void oakcommon_commandlinepositionalargument_free(
	OakCommandLinePositionalArgument *argument)
{
	oakcommon::free_handle(argument);
}
