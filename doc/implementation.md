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
4.  [Garbage Collection](#gc)
5.  [Memory Allocation](#memory)
6.  [References](#references)

### *fLisp* Objects

#### Object Size

*fLisp* implements Lisp objects with a minimal memory footprint. The
object struct contains the *type*, the *size* in bytes of the object and
a union which is a unique field for all object types. The biggest object
type holds three pointers and a `size_t` field - which typically is also
`sizeof(void *)`, so a normal object needs about 48 bytes. No space is
reserved for a documentation string or properties. Therefore the garbage
collector only copies over a minimal, and normally fixed, amount of
memory.

There are two <span class="dfn">string type</span> objects: *symbols*
and *strings*. Since they can have arbitrary length, the *size* field is
introduced. When allocating a string type object fLisp allocates the
required size to hold the entire string in the object space. In all
cases the *size* field is set to the total length of the object.

The downside of this design is, that it is not feasible to use *fLisp*
for applications with very large strings because of the high memory
demand on the semi spaces and the effort to copy them around with each
garbage collection cycle. An application which wants to work with large
strings would instead implement an external mechanism for string
handling, like it is done with the Femto editor.

#### Constant Objects

Several symbols are predefined in C code and bound to some value in
the root environment, examples are `t` and `nil` which are bound to
themselves. *fLisp* does neither garbage collect such symbols, nor does
it allow to bind them to a different value, thus effectively creating
immutable bindings – constants. This technique is used to define type
and error symbols which are then easy to compare by pointer comparision.

[^](#toc)

### Garbage Collection

*fLisp* implements a variant of [Cheney's copying garbage
collector](https://en.wikipedia.org/wiki/Cheney%27s_algorithm), with
which memory is divided into two equal halves (semi spaces): from- and
to-space. From-space is where new objects are allocated, whereas
to-space is used during garbage collection. The from-space part of the
memory is also called the <span class="dfn">Lisp object space</span>.

When garbage collection is performed, objects that are still in use
(live) are copied from from-space to to-space. To-space then becomes the
new from-space and vice versa, thereby discarding all objects that have
not been copied.

The *fLisp* garbage collector
[mmap()](https://man7.org/linux/man-pages/man2/mmap.2.html)'s the
to-space when garbage collection starts and unmaps the from-space
afterwards. If after garbage collection the free space is less then the
required memory (plus some reserved space for exception reporting) the
memory is increased by a multiple of the amount specified in the C-macro
`FLISP_MEMORY_INC`, defined in `lisp.h`. The multiple is calculated to
hold at least the additional requested space. This allows the object
space to grow on demand.

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

`input.path`  
The path string object of the input stream.

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
value,  but at least to `FLISP_MEMORY_INC_SIZE` which defaults to eight
kilobytes.  An application should initialize the semi-space size to a
value above its typical object space demand to avoid repeated garbage
collection cycles on startup.

The `flisp` command line utility which loads just the file extension and
the core Lisp library grows its object space to about 120kB during
startup.

Some other compile time adjustable limits in `lisp.h`:

Input buffer  
2048, `INPUT_FMT_BUFSIZ`, size of the formatting buffer for
`lisp_eval()` and for the input buffer of `(fgets)`.

Output buffer  
2048, `WRITE_FMT_BUFSIZ`, size of the output and message formatting
buffer.

### Unicode / UTF-8

*fLisp* has basic support for Unicode via the UTF-8 encoding.

The Lisp reader is agnostic of Unicode. Syntactic elements and symbols
are ASCII only. An invalid symbol is printed when if it is ASCII,
otherwise the hex code is printed. This leads to several error messages
when a non-ASCII character is entered.

Strings are read in as-is, non-ASCII characters between double quotes
are stored as they appear. The string primitives: `string-length`,
`string-search` and `substring` have a notion of the size of UTF-8
character encodings and count the string indices and lenghts in Unicode
characters instead of bytes.

The primitive `byte-length` returns the length of the character array
used to store the string.

*Caution*: *fLisp* has no measures against incorrectly encoded UTF-8
string.

[^](#toc)

### References

The following is a list of references used for designing the *fLisp*
language, specifically the Lisp libraries. All credits for *fLisp* goes
to the work of the Authors of these works.

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
