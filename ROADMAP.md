-*- mode: markdown; fill-column: 80 -*-

# ROADMAP

## Next

## Future

- Objects host their writer (and reader?) and are pluggable.
- RPN micro reader
- Remove all printf, make own buffering and remove stdio
- Make memory allocated parametrizable, allocate constants in separate mmap.
- Implement backquote and friends.
  - The reader already implements '`', ',' and ',@' as `quasiquote`, `unquote`
    and `splice-unquote`.
- Size reduction:
  - Reduce binary operators to 'and' and 'xor' and write needed rest in Lisp.
- Tap the potential of the in code documentation via Doxygen.
- String size restriction
  - Memory allocator restricts the size of string objects.
  - Option to dynamically adjust or not.
  - Default size = max input string
  - Rationale: embedding, 32, 16, 8 bit versions
- posit's: https://en.wikipedia.org/wiki/Unum_(number_format)
- Event based I/O
  - Buffered I/O operations throw yield exception if buffers are full (w) /empty
    (r).
- Test more then one interpreter.
- ? CSP between interpreters?

## fLisp 0.17
- Make workable flisp command line utility as shell script and rework repl.
- Remove argv0 and argv from flisp_new(), inject them at startup
- More testing, stress-testing
- Femto integration

## flisp 0.17α
- No exceptions/throw/catch, errors are signalled by returning an error object.
- `flisp_eval_input()` replaces `flisp_eval`, no direct string evaluation.
  anymore, return value is result, can be error object.
- Built-in extended objects: interpreter, extension, values (experimental).
- evalExpr() cycles countdown counter.
- gc, gc-always, read trace, primitive trace accessible from Lisp.
- Builtin integer to text converter, also exposed as (ifmt)
- Writer doesn't use printf anymore
- Error handling almost w/o printf
- Reader partially rewritten.
- Reader macros, aka #, some basic macros.
- UTF-8 moved to string extension.
- More flisp_* C-macros and exported functions
- (elements), (object-length) and (object-size) primitives as foundation for
  several higher level functions.
- Fixes in memory allocator and stream code.

## flisp 0.16
- Extensible Lisp Object structure.
- Use size field of strings for (string-length) - speed up.
- flisp_expr()
- Implement error type
- (interp gc) => memory info
- Better gc(), improved gc logging

## flisp 0.15
- basic utf-8 support.
- Implement fnmatch() primitive. https://pubs.opengroup.org/onlinepubs/9799919799/functions/fnmatch.html
- Implement (interp debug[ fd])
- Make shebangable:
  - Implement reader macros, start with #! as comment to end of line.
  - Suppress startup messages:
	- Implement (interp output[ fd])
	- In flisp.c initialize output with the debug fd (or nil)
	- In init.sht (interp output stdout) before entering the interactive repl.
   - Don't start the repl if Lisp files are specified on the command line.

## flisp 0.14
- GC starting with the first loaded symbol.
- Extensions are loaded dynamically
- double extension and file extension are optional
- Fixed: (cond ('(0))) tries to evaluate (0) but should return (0)
- Add (intern string) primitive.
- Add (interp ..) intospection primitive.
- Add/Fix: if, if-not, when, unless
- Add accessors: cadr, cddr, caddr, caar, cdar, caaar, cdaar.
- Add let*, prog1, min, max, join
- Remove string-contains Lisp implementation, it is covered by string-search.
- Remove (reduce), was incompatible and unused, replacement fold-left and family.
- Add string-empty-p string-split and string-join to the string
  library. Documentation for the complete library.
- Add new "file" library: mkdir, file-name-directory, file-name-nondirectory,
  file-name-extension.
- Consolidate more features in core.lsp, remove stdlib.lsp
- Add property list accesors.
- Unify error messages.
- Implement getcwd and fttyp.
- Implement repl in Lisp.


## fLisp 0.13
- Implemented interp introspection and configuration command with version and
  input subcommands.
- Implemented simple repl in Lisp and minimized flisp.c
- Replace setq and define with bind in the core. setq is defined in core.lsp
- Moved append, fold-left, flip, reverse, apply, print, princ to core.lsp
- Renamed os.env to getenv and move to file extension.
- Unified stdlib into flisp.lsp (again).
- Moved (system) to file extension.

## fLisp 0.12

- dynamic memory allocation.
- FLISP_RESULT macros have interp as parameter.
- chunk size and initial memory allocations tuned to femto.
- all memory allocation related Note's fixed.
- optional primitive trace mechanism.
- bitwise integer operations.
- file: feof, fgets, popen, pclose, fstat, mkdir; documentation.
- Poor man's unit test framework in Lisp.

## fLisp 0.11

- Showcase lisp_eval2 with (catch (fread)) mechanism.
- string search
- string-to-number in Lisp by using (read f) from a memory stream.

## fLisp 0.10

- double extension
- variadic multi-typed arithmetic
- (same) primitive, (eq) in Lisp
- fold, unfold, iota

## fLisp 0.9

- core uses only 64 bit integers.
- string-search in core.
- type-of
- all type predicates except null and consp in Lisp.

## fLisp 0.8

- error and object types are Lisp symbols instead of C-enums.

