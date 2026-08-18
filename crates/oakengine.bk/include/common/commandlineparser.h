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
#include "common/handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Neutral by-value handle to a command-line parser instance.
 *
 * Ownership/count semantics follow the convention in common/handle.h:
 * init returns a handle whose object has reference count 1,
 * addref(ctx)/release(ctx) adjust it atomically, and release destroys
 * the object at zero. abi_version is always OAKCOMMON_ABI_VERSION.
 */
typedef struct OakCommandLineParser {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakCommandLineParser;

/**
 * @brief Neutral by-value handle to a registered command-line option.
 *
 * The handle is released with oakcommon_commandlineoption_free() (or
 * handle.release(handle.ctx)); the underlying option is owned by the
 * parser and stays valid until the parser is destroyed. abi_version is
 * always OAKCOMMON_ABI_VERSION.
 */
typedef struct OakCommandLineOption {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakCommandLineOption;

/**
 * @brief Neutral by-value handle to a registered positional argument.
 *
 * The handle is released with
 * oakcommon_commandlinepositionalargument_free() (or
 * handle.release(handle.ctx)); the underlying argument is owned by the
 * parser and stays valid until the parser is destroyed. abi_version is
 * always OAKCOMMON_ABI_VERSION.
 */
typedef struct OakCommandLinePositionalArgument {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakCommandLinePositionalArgument;

/**
 * @brief Create a command-line parser.
 *
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OakCommandLineParser oakcommon_commandlineparser_init(void);

/**
 * @brief Release one reference to a command-line parser.
 *
 * Convenience wrapper around handle.release(handle.ctx): decrements the
 * atomic reference count and destroys the parser (invalidating all
 * option and positional-argument handles created from it) when the
 * count reaches zero. No-op when parser is NULL or parser->ctx is NULL.
 */
void oakcommon_commandlineparser_free(OakCommandLineParser *parser);

/**
 * @brief Set the application name/version shown by print_help.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_set_app_info(OakCommandLineParser parser,
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
 * @param out_option Receives the option handle (reference count 1).
 *        May be NULL if unused.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_add_option(
	OakCommandLineParser parser, const char *const *names, int name_count,
	const char *description, int takes_arg, const char *arg_placeholder,
	int hidden, OakCommandLineOption *out_option);

/**
 * @brief Register a positional argument.
 *
 * @param out_argument Receives the argument handle (reference count 1).
 *        May be NULL if unused.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_add_positional_argument(
	OakCommandLineParser parser, const char *name,
	const char *description, int required,
	OakCommandLinePositionalArgument *out_argument);

/**
 * @brief Parse an argv-style argument list.
 *
 * argv[0] is skipped as the program name, matching C main() convention.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_process(OakCommandLineParser parser,
										const char *const *argv, int argc);

/**
 * @brief Print usage/help text to stdout.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineparser_print_help(OakCommandLineParser parser,
										   const char *filename);

/**
 * @brief Query whether an option was present on the command line.
 *
 * @param is_set Receives the result. Must not be NULL.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineoption_is_set(OakCommandLineOption option,
									   bool *is_set);

/**
 * @brief Release one reference to an option handle.
 *
 * Convenience wrapper around handle.release(handle.ctx). Does not
 * unregister the option from the parser. No-op when option is NULL or
 * option->ctx is NULL.
 */
void oakcommon_commandlineoption_free(OakCommandLineOption *option);

/**
 * @brief Get an option's argument value (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineoption_get_setting(OakCommandLineOption option,
											char *buf, int buf_size);

/**
 * @brief Set an option's argument value.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlineoption_set_setting(OakCommandLineOption option,
											const char *value);

/**
 * @brief Get a positional argument's value (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlinepositionalargument_get_setting(
	OakCommandLinePositionalArgument argument, char *buf, int buf_size);

/**
 * @brief Set a positional argument's value.
 *
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_commandlinepositionalargument_set_setting(
	OakCommandLinePositionalArgument argument, const char *value);

/**
 * @brief Release one reference to a positional argument handle.
 *
 * Convenience wrapper around handle.release(handle.ctx). Does not
 * unregister the argument from the parser. No-op when argument is NULL
 * or argument->ctx is NULL.
 */
void oakcommon_commandlinepositionalargument_free(
	OakCommandLinePositionalArgument *argument);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_COMMANDLINEPARSER_H
