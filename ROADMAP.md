-*- mode: markdown; fill-column: 80 -*-

# ROADMAP

## Next

## Future

- expose the write_* functions in lisp.h, so extensions writes can use them
- Make symbol names huffman or algorithmic compressed int64_6 arrays for
  speedier lookups.
- Add line , character and column counter to input stream
- Expose reader primitives and write reader in Lisp, utf-8!
- Allow to extend the reader macros, property list?
- Add "weight" field to objects and increment them on use. When over threshold
  move to "sink" space and set weight negative. -1 is resevered for constants.
- Re-order symbols on the fly so that heavier ones float to the bottom.
- Objects host their reader (?) and are pluggable.
- RPN micro reader
- Remove all printf, make own buffering and remove stdio <- not a goal anymore:
  simplify to standard.
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


## fLisp 0.18
- Make cloneList() available to Lisp as (list-append) and use e.g. in (append).
- Make memory allocated parametrizable, allocate constants in separate mmap.
- Add "trim" parameter to gc call: add or increase allocated memory.
- Add "which" parameter to gc call, so we can have more then one space.
- Add gc stat fields to interp object


## fLisp 0.17
- Allow all characters except controls and (ASCII) whitespace for symbol names
- Implement backquote and friends.
- Implement multiple return values.
- make (length list) use flisp_list_length if appropiate or make object-length
  for conses return list length instead of 2.
- Consider returning the element instead of the list with one element when
  (elements o n n+1).
- Review error behavior for all primitives.
- Clean up and document internal and exported flisp_* functions and FLISP_* macros.
- ! don't! Remove argv0 and argv from flisp_new(), inject them at startup <- or maybe not.
- More testing, stress-testing.
- Femto integration.
- Cleaner object types:, base object and object extension.
- Lisp objects host their writer function.
- `store` function to (destructively) set a slot's value in an extensible object.


## flisp 0.17α
- Make workable flisp command line utility as shell script and rework repl.
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

