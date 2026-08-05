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

#ifndef OAK_EDITOR_COMMANDLINEPARSER_H
#define OAK_EDITOR_COMMANDLINEPARSER_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a command-line parser instance.
 */
typedef struct OakCommonCommandLineParser OakCommonCommandLineParser;

/**
 * @brief Opaque handle to a registered command-line option.
 *
 * The handle wrapper is freed with oakcommon_commandlineoption_free();
 * the underlying option is owned by the parser and stays valid until
 * the parser is freed.
 */
typedef struct OakCommonCommandLineOption OakCommonCommandLineOption;

/**
 * @brief Opaque handle to a registered positional argument.
 *
 * The handle wrapper is freed with
 * oakcommon_commandlinepositionalargument_free(); the underlying argument
 * is owned by the parser and stays valid until the parser is freed.
 */
typedef struct OakCommonCommandLinePositionalArgument
	OakCommonCommandLinePositionalArgument;

/**
 * @brief Create a command-line parser.
 *
 * @return Parser handle, or NULL on allocation failure.
 */
OakCommonCommandLineParser *oakcommon_commandlineparser_init(void);

/**
 * @brief Destroy a command-line parser.
 *
 * Destroys all option and positional-argument handles created from it.
 * NULL is a no-op.
 */
void oakcommon_commandlineparser_free(OakCommonCommandLineParser *parser);

/**
 * @brief Set the application name/version shown by print_help.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_set_app_info(OakCommonCommandLineParser *parser,
											 const char *name,
											 const char *version);

/**
 * @brief Register an option with one or more name strings.
 *
 * @param names Array of option name strings (without leading dash).
 * @param name_count Number of entries in names. Must be > 0.
 * @param description Help text, may be NULL.
 * @param takes_arg Non-zero if the option consumes the following argument.
 * @param arg_placeholder Placeholder shown in help, may be NULL.
 * @param hidden Non-zero to omit from help output.
 * @param out_option Receives the option handle. May be NULL if unused.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_add_option(
	OakCommonCommandLineParser *parser, const char *const *names, int name_count,
	const char *description, int takes_arg, const char *arg_placeholder,
	int hidden, OakCommonCommandLineOption **out_option);

/**
 * @brief Register a positional argument.
 *
 * @param out_argument Receives the argument handle. May be NULL if unused.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_add_positional_argument(
	OakCommonCommandLineParser *parser, const char *name,
	const char *description, int required,
	OakCommonCommandLinePositionalArgument **out_argument);

/**
 * @brief Parse an argv-style argument list.
 *
 * argv[0] is skipped as the program name, matching C main() convention.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_process(OakCommonCommandLineParser *parser,
										const char *const *argv, int argc);

/**
 * @brief Print usage/help text to stdout.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_print_help(OakCommonCommandLineParser *parser,
										   const char *filename);

/**
 * @brief Query whether an option was present on the command line.
 *
 * @param is_set Receives the result. Must not be NULL.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineoption_is_set(OakCommonCommandLineOption *option,
									   bool *is_set);

/**
 * @brief Free an option handle wrapper.
 *
 * Does not unregister the option from the parser. NULL is a no-op.
 */
void oakcommon_commandlineoption_free(OakCommonCommandLineOption *option);

/**
 * @brief Get an option's argument value (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineoption_get_setting(OakCommonCommandLineOption *option,
											char *buf, int buf_size);

/**
 * @brief Set an option's argument value.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineoption_set_setting(OakCommonCommandLineOption *option,
											const char *value);

/**
 * @brief Get a positional argument's value (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlinepositionalargument_get_setting(
	OakCommonCommandLinePositionalArgument *argument, char *buf, int buf_size);

/**
 * @brief Set a positional argument's value.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlinepositionalargument_set_setting(
	OakCommonCommandLinePositionalArgument *argument, const char *value);

/**
 * @brief Free a positional argument handle wrapper.
 *
 * Does not unregister the argument from the parser. NULL is a no-op.
 */
void oakcommon_commandlinepositionalargument_free(
	OakCommonCommandLinePositionalArgument *argument);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_COMMANDLINEPARSER_H
