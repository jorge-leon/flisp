# *fLisp* History

This document contains some notes on the evolution of *fLisp*, if you
need a reference manual read the [fLisp Manual](flisp.html)
[(Markdown)](flisp.md).

The original [Femto](https://github.com/hughbarney/femto) editor came
initially with a modified version
of [Tiny-Lisp](https://github.com/matp/tiny-lisp) by Matthias Pirstitz.

In 2023 some severe bugs surfaced in Tiny-Lisp.  Georg Lehner
decided, that simplifying the code would help finding them and
started refactoring both C and Lisp code of the Femto project.

- Lisp startup code is loaded from a file, instead of baking it into the
  C - source code.
- Command line arguments are handed over to Lisp.
- A Lisp library directory can be defined at compile time.
- The core Lisp functions are reduced to the bare minimum, all functions
  which can be derived in Lisp are factored out into a core, flisp,
  stdlib and femto Lisp library.
- Lisp functions are made consistent
  with [Elisp](https://www.gnu.org/software/emacs/manual/elisp.html) or [Common
  Lisp](https://lisp-lang.org) as far as it seems to make sense.

The amount of deviation gave merit to renaming the Lisp interpreter from
Tiny-Lisp to fLisp.

In 2024 error and input stream handling was improved in order to
diagnose further known bugs. This lead again to a rewrite of a
great part of the code base:

- A stream object type is added to the Lisp core, together with
  the minimum primitives needed for the Lisp reader and writer.
- An extension mechanism is added to the build system, which allows
  to build the Lisp core alone, or together with a selection of
  C extensions.
- The femto related C - extensions are separated out into such
  an extension.
- A minimal file extensions exposes the core stream function plus
  some additional functions for testing.
- The C interface of the fLisp interpreter is changed: an
  *fLisp* interpreter is instantiated via the `lisp_init()` function,
  which returns an interpreter "object".  This is configured with
  input, output and debug streams and then used for the invocation
  of `lisp_eval()` or `lisp_eval_string()`. This re-architecturing
  allows for centralized access to the interpreters state.
- A command line wrapper `flisp` is added, which implements a simple
  repl using only the Lisp core plus the file extension.

In 2025 garbage collection and Lisp facilities are improved:

- Garbage collecting of stack items is changed, the garbage collector is
  tested extensively and surfaced bugs fixed.
- Exceptions are catched in Lisp and exception codes are Lisp symbols
  instead of numbers.
- Lisp Object types are stored as Lisp symbols instead of C constants.
  This allows for type testing in Lisp.
- Numbers are represented by 64 bit integers instead of double floats.
- The Lisp libraries are extended and functions are improved for
  compatibilty.
- Lisp object space adjusts dynamically when needed.
- The `setq` primitve is replaced by `bind` and then implemented as Lisp
  macro.
- Enough framework is built to implement the repl completely in Lisp.

In 2026 *fLisp* is factored out of *Femto*, C-extensions can be loaded
on demand and extensible objects are introduced. Upon suggestion of
Kirill L error handling is switched from exception handling to returning
error objects and the `fl` and `flisp` command line utilities can be
used for Lisp scripting.

- The fLisp core, string and flisp libraries as well as the file
  extension primitives are refined and complemented, a Lisp file library
  is added, the stdlib library is removed.
- The read-eval-print loop is now completely written in Lisp, `fl` and
  `flisp` can be used for scripting.
- All symbols and primitives are loaded dynamically and argument
  checking now relies only on Lisp object types.
- Public symbols are prefixed with `flisp_` to avoid name clashes when
  embedding.
- Debian packages `flisp` and `flisp-dev` can be built.
- *fLisp* can be embedded now in other applications without the need for
  the *Femto* or *fLisp* sources.
- Basic Unicode / UTF-8 support is added.
- Extensible Objects are introduced, this allows to add new object types
  in extensions.
- Error type and (error) object constructor is added. Exceptions are
  removed completely from core functions as well as fatal error exits.

[^](#top)
