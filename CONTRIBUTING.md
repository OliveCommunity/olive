# Contributing to Oak

Thank you for your interest in contributing to Oak Video Editor!

## Writing code

Code contributions are welcome. Note that the code base is rapidly changing in
the current stage of development however. There is some documentation in the form
of code comments, including Javadoc in header files. Feel free to reach out via
an issue or pull request if you have questions about the architecture or
implementation details.

### Code Standards

In order to keep the code as readable and maintainable as possible, code
submitted should abide by the following standards:

* The code style generally follows the
  [Linux Kernel Coding Style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
  with the following project-specific exceptions and notes:
  * Indentation uses **tabs**, not spaces.
  * Documentation comments should use **Javadoc-style** (`/** ... */`) where appropriate.
* Naming rules (enforced by `readability-identifier-naming` in `.clang-tidy`):
  * Types (`class`, `struct`, `enum`, type aliases, template parameters): `PascalCase`
  * `typedef` of structs is permitted (e.g. the opaque-handle pattern `typedef struct OakEngineNode OakEngineNode;`); struct typedefs follow `PascalCase`
  * Functions, variables, member variables: `snake_case`
  * Private/protected members: trailing underscore, `class_member_variables_`
  * Constants and enum values: `snake_case` (e.g. `k_dry_run_interval`, `k_linear`); `ALL_CAPS` is reserved for macros — save the fear for things that are actually dangerous
  * Macros: `OAK_ALL_CAPS` (project prefix), and avoid them when a constant or function will do
  * File names: all lowercase, `mystring.h` / `mystring.cpp`
  * Namespaces: short `snake_case`
  * Getters: same name as the private member without the trailing underscore (`foo_` → `foo()`); setters: `set_foo()`
  * Exception: Qt and third-party (e.g. OpenFX) virtual overrides and framework callbacks keep their original names (`paintEvent`, `getParams`, ...) — renaming them would break the override
* Tests are written with **Google Test** (`TEST`/`TEST_F`/`TEST_P` + `EXPECT_*`/`ASSERT_*`). Do not add hand-written test `main()`s, raw `assert()`-based test files, or custom test macros/frameworks. CTest stays the runner only — register cases through `gtest_discover_tests()`; use `GTEST_SKIP()` for environment-dependent cases (GPU, missing codecs) instead of relying on crashes or timeouts..
* 100 column limit (where it doesn't impair readability)
* Unix line endings (only LF no CRLF)
