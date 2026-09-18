# *fLisp* Implementation Details

### Introduction

This document discusses some of the design decisions taken in *fLisp*.
Other documentation topics are:

- [fLisp Manual](flisp.html) [(Markdown)](flisp.md)
- [Development](develop.html) of and
  with *fLisp* ([Markdown](development.md))
- [History](history.html) ([Markdown](history.md))

### Table of Contents

1.  [Introduction](#introduction)
2.  Table of Contents
3.  [*fLisp* Objects](#objects)
    1.  [Object Size](#object_size)
    2.  [Constant Objects](#constant_objects)
4.  [The Type System](#types)
5.  [Error Handling](#errors)
6.  [Multivalue Returns](#values)
7.  [Unicode / UTF-8](#utf8)
8.  [Garbage Collection](#gc)
9.  [Memory Allocation](#memory)
10. [References](#references)

### *fLisp* Objects

#### Object Size

*fLisp* implements Lisp objects with a minimal memory footprint. No
space is reserved for a documentation string, function slots or other
properties. Objects contain a fixed sized structure and optionally a
variable length extensions structure. The fixed fields are: the
*type* of the object, the *size* of the extension structure in bytes and
the *length* which is the number of Lisp objects stored in the extension
structure. The Lisp Objects are stored in an array at the start of the
extension, any extra space allocated by size is used as space for
C-level data. The garbage collector copies first the whole object and
then all embedded Lisp objects in the extension structure.

Strings and symbols are a special case where *length* is 0 and *size*
indicates the byte length of the respective string.

When *size* is 0 we talk about <span class="dfn">simple objects</span>.
With simple objects the *length* field is reused either as a 64 bit
integer or as a pointer to some C-level data. The *size* field is of
`size_t`, so it is big enough to address the available address space of
the underlying hardware architecture.

The *fLisp* core uses simple objects for integers, double floats,
constant strings (`type-str`) and pointers to Lisp primitives.

The simple objects amounts to three pointers. On a 64 bit architectures
this requires 24 bytes. The biggest core object is of
`type-interpreter`, it requires a total of 112 bytes.  The most common
object type is the *cons*, which holds two Lisp objects and requires 40
bytes.

The two <span class="dfn">string type</span> objects: *symbol* and
*string* can have arbitrary length. When allocating a string type object
*fLisp* allocates the required size to hold the entire string in the
object space. The downside of this design is, that it is not feasible to
use *fLisp* for applications with very large strings because of the high
memory demand on the semi spaces and the effort to copy them around with
each operation (objects are inmutable) as well as during the garbage
collection cycle. An application which wants to work with large strings
would instead implement an external mechanism for string handling, like
it is done with the Femto editor.

#### Constant Objects

Several symbols are predefined in C code and bound to some value in
the root environment, examples are `t` and `nil` which are bound to
themselves. *fLisp* does not garbage collect symbols outside the semi
spaces nor does it allow to bind them to a different value, thus
effectively creating immutable bindings – constants. This technique is
used to define type and error symbols which are then easy to compare by
pointer comparision.

Constant symbols are of `type-symbol` but do not store their string in
the extension structure. Their *size* is 0 and the *length* field is
used as a pointer to a static C-string with their name. *fLisp*
distinguishes constant symbols  from Lisp created ones by looking at the
*size*.

### The Type System

The *type* field of a Lisp object is by itself a Lisp objects of
`type-type`. A type object is of length three and has the fields *name*,
*new* and *write*. The *name* field is a symbol and must start with the
prefix `type-`. *new* and *write* are primitives for creating a new
object/writing an object of the respective type. If they are set to
`nil` a default creator/writer is used.

### Error Handling

All Lisp primitives either return the result of their operation, or an
error object. Error objects have a length of three and contain the
fields *type*, *message* and *culprit*. *type* must be a symbol,
*message* must be a string.

Lisp primitives check their parameter types either already in the
evaluator or before processing the parameters. With a type mismatch they
return an error before using the parameter, thus errors as parameters
are often returned as *culprits* of new error messages.

The following primitives do not err on errors as their arguments:
`write`, `null`, `type-of`, `consp`, `same`, `elements`, `object-size`,
`object-length`, `new`, `store`.

### Multivalue Returns

A sexp evaluates to exactly one object. *fLisp* supports returning
multiple return values with the *values* type. A
<span class="dfn">values</span> objects contains a single element, which
must be the first car of a list.

When the evaluator of a lambda, a macro or a primitive encounters an
argument of `type-value`, the value is discarded and its values list is
spliced into the argument list. After splicing processing continues with
the first element of the values list. Thus values are replaced
recursively.

Note that Lisp code will never “see” a value object.

[^](#toc)

### Unicode / UTF-8

*fLisp* has basic support for Unicode via the UTF-8 encoding.

The Lisp reader is agnostic of Unicode. Syntactic elements and symbols
are ASCII only. When an invalid symbol character is encountered it is
printed ASCII characters when possible, otherwise its hex code is
printed. Unicode multi-byte characters produce as many error messages as
there are bytes.

Strings are read in as-is, non-ASCII characters between double quotes
are stored as they appear. The string extension primitives:
`string-length`, `string-search, string-spn, string-cspn` and
`substring` have a notion of the size of UTF-8 character encodings and
count the string indices and lenghts in Unicode characters instead of
bytes.

The primitive `(object-size «object»)` returns the length of the
character array used to store the string, including the terminating
`NUL` character.

*Caution*: *fLisp* takes no measures against incorrectly encoded UTF-8
string.

The string extension primitives however perform some checks.

[^](#toc)

### Garbage Collection

*fLisp* implements a variant of [Cheney's copying garbage
collector](https://en.wikipedia.org/wiki/Cheney%27s_algorithm), with
which memory is divided into two equal halves (semi spaces): *from* and
*to* space. <span class="dfn">from</span> space is where new objects are
allocated, whereas <span class="dfn">to</span> space is used during
garbage collection. The *from* space part of the memory is also called
the <span class="dfn">Lisp object space</span>.

When garbage collection is performed, objects that are still in use
(live) are copied from *from* space to *to* space. *to* space then
becomes the new *from* space and vice versa, thereby discarding all
objects that have not been copied.

The *fLisp* garbage collector
[mmap()](https://man7.org/linux/man-pages/man2/mmap.2.html)'s the *to*
space when garbage collection starts and unmaps the *from* space
afterwards. If after garbage collection the free space is less then the
required memory (<span class="mark">plus some reserved space for
exception reporting</span>) the memory is increased by a multiple of the
amount specified in the C-macro `FLISP_MEMORY_INC_SIZE`, defined in
`lisp.h`. The multiple is calculated to hold at least the additional
requested space. This allows the object space to grow on demand.

The garbage collector takes as input a list of root objects. Objects
that can be reached by recursively traversing this list are considered
live and will be moved to to-space. When we move an object, we must also
update its pointer within the list to point to the objects new location
in memory.

The root objects of an fLisp interpreter are found in the following
fields of the `Interpreter` struct:

`gcTop`  
List of active variables along the call stack.

`symbols`  
The list of Lisp symbols.

`global`  
The root environment.

`result`  
The result object of the `flisp_eval()` call.

`error`  
The error type symbol of the `flisp_eval()` call.

`debug.path`  
 

`input.path`  
 

`output.path`  
The path string object of the debug, input and output stream.

With respect to the active variables in the call stack the interpreter
cannot use raw pointers to objects: any function that might trigger
garbage collection would move the object pointed to, causing a SEGV when
accessing the pointer. Instead, objects must be added to the `gcTop`
list and then only accessed through the pointers inside the list.

Thus, whenever we would have used a raw pointer to an object, we use a
pointer to the pointer inside the list instead:

            function:              pointer to pointer inside list (Object **)
            |
            v
            list of root objects:  pointer to object (Object *)
            |
            v
            semi space:             object in memory
          

*GC_TRACE(gcX, X)* add object *X* to the `gcTop` list and declares the
variable *gcX* which points to the objects pointer inside the list. The
list chains are uniquely named variables based on the line number of the
`GC_TRACE` macro. Therefore each `GC_TRACE` invocation must occur on a
different line.

Information about each garbage collection process and memory status is
written to the debug file descriptor. By recompiling *fLisp* with
`DEBUG_GC` set to 1 much more debug information is produced. Setting
`FLISP_TRACK_GCTOP` to 1 will add information about garbage collection
of the active variables in the call stack. Finally every now and then
the *fLisp* garbage collector is stress tested by setting
`DEBUG_GC_ALWAYS` to 1. This forces garbage collection on **each**
object allocation.

[^](#toc)

### Memory Allocation

`lisp_new()` sets the initial size of the semi-spaces to the given
value,  but at least to `FLISP_MEMORY_INC_SIZE` which defaults to 16
kilobytes.  An application should initialize the semi-space size to a
value above its typical object space demand to avoid repeated garbage
collection cycles on startup.

The `flisp` command line utility which loads just the file extension and
the core Lisp library grows its object space to about 100kB during
startup, femto requires about 345kB.

Some other compile time adjustable limits in `lisp.h`:

Input buffer  
2048, `INPUT_FMT_BUFSIZ`, size of the formatting buffer for
`lisp_eval()` and for the input buffer of `(fgets)`.

Output buffer  
2048, `WRITE_FMT_BUFSIZ`, size of the output and message formatting
buffer.

### References

The following is a list of references used for designing the *fLisp*
language, specifically the Lisp libraries. All credits for *fLisp* goes
to the Authors of these works.

1.  [Tiny Lisp](https://github.com/matp/tiny-lisp)
2.  [Emacs
    Lisp](https://www.gnu.org/software/emacs/manual/html_mono/elisp.html)
3.  [Common
    Lisp](https://www.lispworks.com/documentation/HyperSpec/Front/)
4.  [let](https://blog.veitheller.de/Scheme_Macros_III:_Defining_let.html)
5.  [curry](https://en.wikibooks.org/wiki/Write_Yourself_a_Scheme_in_48_Hours/Towards_a_Standard_Library) 
6.  [mal - quasiquote](https://github.com/kanaka/mal)
7.  [Scheme](https://www.scheme.org/) 
8.  [Scheme v7 Standard](https://standards.scheme.org/official/r7rs.pdf)
9.  [TSPL2d](https://www.scheme.com/tspl2d/)
