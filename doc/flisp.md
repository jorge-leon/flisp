# fLisp Manual

### Introduction

> A designer knows he has achieved perfection not when there is nothing
> left to add, but when there is nothing left to take away.
>
> — Antoine de Saint-Exupery

*fLisp* is a tiny yet practical interpreter for a dialect of the Lisp
programming language. It can be embedded into other applications and is
extensible via C libraries. *fLisp* is hosted at
[Github](https://github.com/jorge-leon/flisp), released to the public
domain and used as extension language for the [*Femto* text
editor](https://github.com/jorge-leon/femto).

*fLisp* is a Lisp-1 interpreter with Scheme like lexical scoping,
tailcall optimization and other Scheme influences. *fLisp* originates
from [Tiny-Lisp by matp](https://github.com/matp/tiny-lisp) (pre 2014),
was integrated into [Femto](https://github.com/hughbarney/femto) by Hugh
Barney (pre 2016) and extended by Georg Lehner since 2023. Read more
about fLisp's history.

This document is the reference manual for *fLisp* version 0.14 or later
([Markdown](flisp.md)). If you want to learn about Lisp programming use
other resources eg.

- The [Common Lisp](https://lisp-lang.org) web site,
- [An Introduction to Programming in Emacs
  Lisp](https://www.gnu.org/software/emacs/manual/html_node/eintr/index.html)
  or
- [The Scheme Programming Language](https://www.scheme.org/).

Other documentation topics:

- [Development](develop.html) of and with *fLisp*
  ([Markdown](development.md))
- [History](history.html) ([Markdown](history.md))
- [Implementation](implementation.html) Details
  ([Markdown](markdown.md))

This manual refers to version 0.14 or later of fLisp.

### Table of Contents

1.  [Introduction](#introduction)
2.  Table of Contents
3.  [Notation Conventions](#notation)
4.  [Lisp](#lisp)
    1.  [fLisp Interpreter](#interpreter)
    2.  [Syntax](#syntax)
    3.  [Objects and Data Types](#objects_and_data_types)
    4.  [Environments, Functions, Evaluation](#evaluation)
    5.  [Error Handling](#exceptions)
    6.  [Global Variables](#globals)
5.  [*fLisp* Primitives](#primitives)
    1.  [Interpreter Operations](#interp_ops)
    2.  [Input / Output and Others](#in_out)
    3.  [Object Operations](#object_ops)
    4.  [Arithmetic Operations](#arithmetic_ops)
    5.  [Bitwise Integer Operations](#bitwise_ops)
    6.  [String Operations](#string_ops)
    7.  [*fLisp* Core Library](#core_lib)
6.  [*fLisp* Extensions](#extend)
    1.  [String Extension](#string)
    2.  [POSIX Extension](#posix)
    3.  [Double Extension](#double)
7.  [Lisp Libraries](#libraries)
    1.  [fLisp Library](#flisp_lib)
    2.  [String Library](#string_lib)
    3.  [File Library](#file_lib)
8.  [`flisp` Command Line Interpreter](#flisp)

### Notation Conventions

We use the following notation rules to describe the *fLisp* syntax:

*name*  
*name* is the name of a variable. In Markdown documents it is shown with
guillemots, like this `«name»`.

`[text]`  
`text` can be given zero or one time.

`[text..]`  
`text` can be given zero or more times.

“` `”  
A single space is used to denote an arbitrary sequence of whitespace.

*fLisp* does not use `[`square brackets`]` and double-dots `..` as
syntactical elements.

Variable names convey the following context:

Lisp object of any type:  
*object* *value* *o* *a* *b* *c*

Program elements:  
*arg* *args* *params* *opt* *body* *expr* *pred* *p*

Integer:  
*i* *j* *k*

Double:  
*x* *y* *z*

Any numeric type:  
*n* *n1* *n2*

Symbol:  
*symbol*

String:  
*string* *s* *s1* *s2* …

List/Cons:  
*cons* *l* *l1* *l2* …

Stream:  
*stream* *f* *fd*

Function/lambda:  
*f*

*fLisp* fancies to converge towards Emacs and Common Lisp, but includes
also Scheme functions. Function descriptions are annotated according to
their compatibility:

<u>C</u>  
Interface compatible, though probably less featureful.

<u>D</u>  
Same name, but different behavior.

<u>S: *name*</u>  
*name* is a similar, but not compatible, function in Emacs Lisp, Common
Lisp or Scheme.

<u>B</u>  
Buggy/incompatible implementation.

<u>f</u>  
fLisp specific function.

<u>d</u>  
Only available when the double extension is loaded.

Compatibility with Emacs is omitted. By default compatibility with
Common Lisp is annotated. The suffix <u>e</u> is used to indicate
reference to Emacs Lisp, <u>s</u> for Scheme. *fLisp* specific function
are annotated with <u>f</u>.

[^](#toc)

### Lisp

#### fLisp Interpreter

*fLisp* processes programs in a three step process:

1.  Read: program text is read in and converted into an internal
    representation.
2.  Evaluate: the internal representation is evaluated.
3.  Print: the result of the evaluation is optionally printed and
    returned to the invoker.

[Core functions](#flisp_primitives) of the language - called
<span class="dfn">primitives</span> - operate on built-in [object
types](#objects_and_data_types). *fLisp* can be extended with
C-libraries to provide additional functions. With respect to the
interpreter processing extension functions behave the same as core
functions.

#### Syntax

Program text is written as a sequence of symbolic expressions -
<span class="abbr"><span class="dfn">sexp</span></span>'s - in
parenthesized form. A [sexp](https://en.wikipedia.org/wiki/S-expression)
is either a single symbol or a sequence of symbols or sexp's enclosed in
parenthesis.

The following characters are special to the reader:

`(`  
Starts a function or macro invocation, a *list* or *cons* object (see
[Objects and Data Types](#objects_and_data_types)).

`)`  
Finishes a function or macro invocation, *list* or *cons* object.

`'` and `:`  
With a single quote or a colon prefix before a
<span class="abbr">sexp</span>, the <span class="abbr">sexp</span> is
expanded to `(quote «sexp»)` before it is evaluated.

`.`  
The expression `(«a» . «b»)` evaluates to a *cons* object, holding the
objects *a* and *b*.

`"`  
Encloses strings.

`\`  
Escape character. When reading a string, the next character is read as
character, even if it is special to the reader.

`;`  
Comment character. When the reader encounters a semicolon it ignores it
and all characters up to the next newline.

\`  
Quasiquote: a backquote before a sexp is expanded to
`(quasiquote sexp)`, with exception of contained `unquote` and
`unquote-splice` sequences.

,  
Expands to `(unquote sexp)`: an unquoted expresion within a quasiquote
is substituted.

,@  
Expands to `(unquote-splice)`: like `unquote`, but if the expresion
evaluates to a list its elements are inserted element by element.

Note: `quasiquote`, `unquote` and `unquote-splice` are not yet
implemented in *fLisp* 0.14.

Numbers are read and written in decimal notation. If the fLisp
interpreter with floating point support is used floating point numbers
are recognized if a sequence of digit contains a dot (.). Exponent
notation is not supported by the reader.

A list of objects has the form:

> `([«element» ..])`

A function invocation has the form:

> `(«name» [«param» ..])`

There are two predefined objects. Their symbols are:

`nil`  
represents: the empty list: `()`, the end of a list marker or the false
value in logical operations.

`t`  
“true”, a predefined, non-false value.

#### Objects and Data Types

*fLisp* objects have one of the following data types:

<span class="dfn">integer</span>  
64 bit singed integer.

double  
Double precission IEEE floating point number. Only available in fLisp
interpreters with double support.

<span class="dfn">string</span>  
Character array. UTF-8 aware.

<span class="dfn">cons</span>  
Object holding two pointers to objects.

<span class="dfn">symbol</span>  
String with restricted character set:
`[A-Z][0-9][a-z]!#$%&*+-./:<=>?@^_~`

In contrast to strings symbols are unique. Two string objects with the
same character string can be different objects.

<span class="dfn">lambda</span>  
Anonymous function with parameter evaluation.

<span class="dfn">macro</span>  
Anonymous function without parameter evaluation.

<span class="dfn">stream</span>  
An input/output stream.

Objects are immutable; functions either create new objects or return
existing ones, with the single exception of the `nreverse` funciton,
which is included for speed.

Characters do not have their own type. A single character is represented
by a *string* with length one.

#### Environments, Functions, Evaluation

All operations of the interpreter take place in an environment. An
<span class="dfn">environment</span> is a collection of named objects.
The object names are of type *symbol*. An object in an environment is
said to be <span class="dfn">bound</span> to its name. An object bound
to symbol *name* is also called to be
the <span class="dfn">value</span> of
the <span class="dfn">variable</span> *name*.

Environments can have a parent. Each *fLisp* interpreter starts with a
<span class="dfn">root</span> environment without a parent, this is also
called the <span class="dfn">global</span> environment.

*lambda* and *macro* objects are functions. They have a parameter list
and a sequence of sexp's as body. When functions are invoked a new
environment is created as child of the current environment. Functions
receive zero or more objects from the caller. These are bound one by one
to the symbols in the parameter list in the new environment.

*lambda*s return the result of evaluating the body in the new
environment.

*macro*s first evaluate the body in the calling environment. The
resulting sexp is evaluated in the new environment and that result is
returned. *macro* bodies are typically crafted to return new sexp's in
terms of the parameters.

When a sexp is evaluated and encounters a symbol it looks it up in the
current environment, and then recursively in the environments from which
the lambda or macro was invoked. The symbol of the first found binding
is then replaced by its bound object.

#### Error handling

Whenever fLisp encounters an error an exception is thrown. Exceptions
have an <span class="dfn">error type</span> symbol a human readable
<span class="dfn">error message</span> and the <span class="dfn">object
in error</span>, which is nil with generic errors. fLisp does not
implement stack backtracking. Exceptions are either caught on the top
level of an evaluation or by a [`catch`](#interp_ops) statement.

The following error type symbols are defined and used internally:

- Read:
  - `read-incomplete`
  - `invalid-read-syntax`
- Eval:
  - `range-error`
  - `wrong-type-argument`
  - `invalid-value`
  - `wrong-num-of-arguments`
  - `arith-error`
- System:
  - `io-error`
  - `out-of-memory`
  - `gc-error`
  - `end-of-file`
  - `permission-denied`
  - `not-found`
  - `file-exists`
  - `is-directory`

Exceptions can be thrown via the [`throw`](#interp_ops) function. As
long as applicable use one of the existing error codes with
`throw` instead of creating new ones.

*fLisp* outputs an error message formated like

> `error: «symbol»: '«object»' «message»`

where *symbol* is the error symbol, *object* is the serialization of the
object causing the error. *message* is the error message.

#### Global Variables

When an fLisp interpreter is created the following symbols are bound in
the root environment:

`*INPUT*`  
Is bound to the initial input stream or to `nil` if there is none.

`*OUTPUT*`  
Is bound to the initial output stream, or to `nil` if there is none.

`argv`  
Is bound to a list of all command line arguments of the invoking program
in order. Each element is of type string.

`argv0`  
Is bound to the name of the invoking program. It is of type string.

`script_dir`  
Is bound to either the hard coded path to the Lisp libraries or the
contents of the environment variable `FLISPLIB`.

[^](#toc)

### *fLisp* Primitives

*fLisp* counts with a minimal set of built-in functions called
<span class="dfn">primitives</span>. The complete *fLisp* language is
then built on top of this primitives.

In the following sub section the documentation of primitives is grouped
by the type of objects they operate on.

#### Interpreter Operations

`(progn[ «expr»..])` ⇒ *val*<sub>*n*</sub>

Each *expr* is evaluated, the value of the last is returned. If no
*expr* is given, `progn` returns `nil`.

`(cond[ «clause»..])` ⇒ *pred*<sub>*n*</sub>\|*val*<sub>*n*</sub>\|`nil`

Each *clause* is of the form `(«pred»[ «action» ..])`. `cond` evaluates
each *clause* in turn. If *pred* evaluates to `nil`, the next *clause*
is tested. If *pred* evaluates not to `nil` and if there is no *action*
the value of *pred* is returned, otherwise `(progn «action» ..)` is
returned and no more *clause*s are evaluated.

`(bind «symbol» «value»[ «globalp»)` ⇒ *value* <u>f</u>

Create or update *symbol* and bind it to *value*. Return value. First
*symbol* is looked up in the current environment, then recursively in
the parent environments. If it is not found, it is created in the
current environment as long as *globalp* is `nil` or omitted. If
*globalp* is not `nil` *symbol* is always created in the global (top
level) environment.

`(lambda «params» «body»)` ⇒ *lambda*

Returns a *lambda* function described by *body*, which accepts zero or
more arguments passed as list in the parameter *params*.

`(lambda ([«param» ..]) «body»)` <u>s</u>

Returns a *lambda* function which accepts the exact number of arguments
given in the list of *param*s.

`(lambda («param»[ «param»..] . «opt») «body»)` <u>s</u>

Returns a *lambda* function which requires at least the exact number of
arguments given in the list of *param*s. All extra arguments are passed
as a list in the parameter *opt*.

`(macro «params» «body»)` ⇒ *macro*

`(macro ([«param» ..]) «body»)` <u>s</u>

`(macro («param»[ «param»..] . «opt») «body»)` <u>s</u>

These forms return a macro function. Parameter handling is the same as
with lambda.

`(quote «expr»)` ⇒ *expr*

Returns *expr* without evaluating it.

`(eval «expr»)` ⇒ *o*

Evaluates *expr* and returns the result.

`(catch «expr»)` ⇒ *errinfo* <u>D</u>

Evaluates *expr* and returns a list with three elements:

*error_type*  
`nil` on success or an error type symbol.

*message*  
A human readable error message.

*object*  
The result of the the expression or the object in error.

`(throw «result» «message»[ «object»])` <u>D</u>

Throws an exception, stopping any further evaluation. *result* is the
error type symbol, *message* is a human readable error string and
*object* is the object in error, if any.

`(interp «cmd»[ «arg»..])` <u>f</u>

Interpreter introspection. The following commands are available

`(interp version)` ⇒ *string* .. returns the version string of fLisp.

`(interp input[ «fd»])` ⇒ *stream* .. returns and optionally sets the
input stream of the interpreter.  
`(interp global)`  ⇒ *env*.. returns the global environment.

`(interp env[ field[ «env»]])`  ⇒ *o*.. returns the
current environment, one of its fields given as symbols `parent`,
`vars`, `vals`, or the given field of the given environemnt *env*.

 

 

 

 

#### Input / Output and Others

`(open «path»[ «mode»])` ⇒ *stream* <u>S</u>

Open file at string *path* with string *mode* and return a stream
object. *mode* is `"r"`ead only by default.

`open` can open or create files, file descriptors and memory based
streams.

Files:  
*path*: path to file, *mode*: one of `r`, `w`, `a`, `r+`, `w+`, `a+`
plus an optional `b` modifier.

File descriptors:  
*path*: `<«n»` for reading, `>«n»` for writing. *n* is the number of the
file descriptor. Omit *mode*.

Memory streams:  
For reading *path* is the string to read, *mode* must be set to: `<`.
The name of the opened file is set to `<STRING`.

For writing *path* is ignored, *mode* must be set to: `>`. The name of
the opened file is set to `>STRING`.

`(close «stream»)` ⇒ *integer* <u>S</u>

Close *stream* object. Returns the value of the systems `close()`
function as integer.

`(file-info «stream»)` ⇒ *info* <u>f</u>

Returns `(«path» «buf» «fd»)` for *stream*. *buf* is either `nil` or the
text buffer of a memory stream. *fd* is either the integer
representation of the file descriptor or `nil` when *stream* is already
closed.

`(read` *stream*`[ «eof-value»])` ⇒ *object* <u>S: read</u>

Reads the next complete Lisp expression from *stream*. The read in
object is returned. If end of file is reached, an exception is raised,
unless *eof-value* is not `nil`. In that case `eof-value` is returned.

`(write «object»[ «readably»[ «fd»]])` ⇒ *object*

Formats *object* into a string and writes it to the default output
stream. When *readably* is not `nil` output is formatted in a way which
which gives the same object when read again. When stream *fd* is given
output is written to the given stream else to the output stream. `write`
returns the *object*.

 

#### Object Operations

`(null «object»)` ⇒ *p*  
Returns `t` if *object* is `nil`, otherwise `nil`. 

`(type-of «object»)` ⇒ *symbol*  
Returns the type symbol of *object*.

`(consp «object»)` ⇒ *p*  
Returns `t` if *object* is of type cons, otherwise `nil`.

`(nreverse «l»)` ⇒ *l'*  
Destructively reverses list *l* and returns it.

`(intern «string»)` ⇒ *symbol*  
Returns the symbol with name *string*. If the symbol does not exist yet
it is created.

`(symbol-name «object»)` ⇒ *string*  
If *object* is of type symbol return its value as string.

`(cons «car» «cdr»)` ⇒ *cons*  
Returns a new cons with the first object set to the value of *car* and
the second to the value of *cdr*.

`(car «cons»)` ⇒ *o*  
Returns the first object of *cons*.

`(cdr «cons»)` ⇒ *o*  
Returns the second object of *cons*.

`(same «a» «b»)` ⇒ *p* <u>f</u>  
Returns `t` if *a* and *b* are the same object, `nil` otherwise.

#### Arithmetic Operations

*fLisp* provides binary 64 bit integer arithmetic. n-ary operations with
type coercion are provided in the core Lisp libraries.

`(i+ «i» «j»)` ⇒ *k* <u>f</u>  
Returns the sum of *i* *j*.

`(i- «i» «j»)` ⇒ *k* <u>f</u>  
Returns *i* minus *j*.

`(i* «i» «j»)` ⇒ *k* <u>f</u>  
Returns the product of *i* *j*.

`(i/ `*`q`*` `*`d`*`)` ⇒ *k* <u>f</u>  
Returns *q* divided by *d*. Throws `arith-error` if *j* is zero.

`(i% `*`q`*` `*`d`*`)` ⇒ *k* <u>f</u>  
Returns the rest (modulo) of the integer division of *i* by *j*. Throws
`arith-error` if *j* is zero.

`(i= «i» «j»)` ⇒ *p* <u>f</u>  
 

`(i< «i» «j»)` ⇒ *p* <u>f</u>  
 

`(i> «i» «j»)` ⇒ *p* <u>f</u>  
 

`(i<= «i» «j»)` ⇒ *p* <u>f</u>  
 

`(i>= «i» «j»)` ⇒ *k* <u>f</u>  
These predicate functions apply the respective comparison operator
between *i* *j* and return either `t` or `nil`.

#### Bitwise Integer Operations

`(& «i» «j»)` ⇒ *k* <u>f</u>  
Returns the bitwise and operation on *i* and *j*.

`(| «i» «j»)` ⇒ *k* <u>f</u>  
Returns the bitwise or operation on *i* and *j*.

`(^ «i» «j»)` ⇒ *k* <u>f</u>  
Returns the bitwise xor operation on *i* and *j*.

`(<< «i» «j»)` ⇒ *k* <u>f</u>  
Returns *i* shift left by *j* bits.

`(>> «i» «j»)` ⇒ *k* <u>f</u>  
Returns *i* shift right by *j* bits.

`(~ «i»)` ⇒ *k* <u>f</u>  
Returns the bitwise negation of *i*.

#### String Operations

`(string-compare «s1» «s2»)` ⇒ *i* <u>f</u>

Returns `0` if strings *s1* and *s2* are the same, a negative intetger
if s1 is less then s2, a positive integer otherwise. See strcmp(3).

`(string-length «string»)` ⇒ *i* <u>f</u>

Returns the length of *string* as a *number*.

`(string-append «s1» «s2»)` ⇒ *string* <u>f</u>

Returns a new string consisting of the concatenation of *string1* with
*string2*.

`(substring «string»[ «start» [«end»]])` ⇒ *string* <u>f</u>

Returns the sub string from *string* which starts with the character at
index *start* and before index *end*. String indexes are zero based,
negative indexes count from the end of *string*. If *end* is not given
it defaults to the end of *string*. If *start* is not given, it defaults
to the start of *string*.

`(string-search «needle» «haystack»)` <u>C</u>

Returns the position of string *needle* if it is contained in
string *haystack*, otherwise `nil`.

 

[^](#toc)

#### *fLisp* Core Library

The core library complements the built in primitives with basic Lisp
programming idioms. It is included in the startup file of the flisp(d)
command line interpreter.

`(list` \[*element* ..\]`)` ⇒ *list*  
Returns the list of all provided elements.

`(defmacro «name» «params» «body»)` ⇒ *macro*  
Defines and returns a macro and binds it to the global symbol *name*.

`(if «pred» «then»[ «else»..])` ⇒ *then\|else<sub>n</sub>*  
If *pred* is not `nil` evaluate and return *then,* otherwise evaluate
all *else* terms in a `progn`.

`(if-not «pred» «else»[ «then» ..])` ⇒ *else\|then<sub>n</sub>*  
If *pred* is not `nil` evaluate and return *else,* otherwise evaluate
all *then* terms in a `progn`.

`(when «pred»[ «then»..])` ⇒ *then<sub>n</sub>\|*`nil`  
If *pred* is not `nil` evaluate and return all *then* terms in a
`progn`, otherwise return `nil`.

`(unless «pred»[ «then»..])` ⇒ `nil`*\|then<sub>n</sub>*  
If *pred* is `nil` evaluate and return all *then* terms in a `progn`,
otherwise return `nil`.

`(defun «name» «params» «body»)` ⇒ *lambda*  
Defines and returns a lambda and binds it to the global symbol *name*.

`(cadr '(a «b»[ «c»..]))` ⇒ *b*  
 

Return second element of a list.

`(cddr '(«a» «b»[ «c»..]))` ⇒ `([«c»..])`  
Return rest of list after second element.

`(caddr («a» «b» «c»[ «d»..]))` ⇒ `([«d»..])`  
Return third element of list.

`(caar '((«a» «b)»[ «c»..]))` ⇒ *a*  
Return first element of first element of list.

`(cdar '((«a» «b)»[ «c»..]))` ⇒ *b*  
Return rest (cdr) of  of first element of list.

`(caaar '(((«a» «b))»[ «c»..]))` ⇒ *a*  
Return first element of first element of first element of list.

`(cdaar '(((«a» «b))»[ «c»..]))` ⇒ *b*  
Return rest (cdr) of first element of first element of list.

`(setq «symbol» «value»[ «symbol» «value»..])` ⇒ *value*  
Create or update named objects: If *symbol* is the name of an existing
named object in the current or a parent environment the named object is
set to *value*, if no symbol with this name exists, a new one is created
in the top level environment. `setq` returns the last *value*.

`(curry («func» «a»))` ⇒ *lambda*  
Returns a lambda with one parameter which returns `(«func» «a» «b»)`.

`(typep («type» «object»))` ⇒ *p*  
Returns `t` if *object* is of type *type*, `nil` otherwise.

`(integerp «object»)` ⇒ *p*  
`(doublep «object»)` ⇒ *p*  
`(stringp «object»)` ⇒ *p*  
`(symbolp «object»)` ⇒ *p*  
`(lamdap «object»)` ⇒ *p*  
`(macrop «object»)` ⇒ *p*  
`(streamp «object»)` ⇒ *p*  
Return `t` if *object* is of the respective type, otherwise `nil`.

`(mapcar «func» «list»)` ⇒ *list'* <u>Se, Dc</u>  
Apply *func* to each element in list and return the list of results.

In Elisp func has to be quoted, in CL variadic *func* operates on a list
of lists.

`(let ((«name» «value»)[ («name» «value»)..]) «body»)` ⇒ *o*  
Bind all *name*s to the respective *value*s then evaluate *body*.

`(let «label»((«name» «value»)[ («name» «value»)..]) «body»)` ⇒ *o* <u>Cs</u>  
Labelled or “named” let: define a local function *label* with *body* and
all *name*s as parameters bound to the *values*.

`(let* ((«name» «value»)[ («name» «value»)..) «body»)` ⇒ *o*  
Sequentually bind all *names* to the respective *values* then evaluate
*body*.

`(prog1 «sexp»[«sexp»..])` ⇒ *p*  
Evaluate all *sexp* in turn and return the value of the first.

`(string «arg»)` ⇒ *string*  
Returns the string conversion of argument.

`(concat` \[*arg*..\]`)` ⇒ *string* <u>Ce</u>  
Returns concatenation of all arguments converted to strings.

`(assert-type «o» «type» «s»)`  
`(assert-number «o» «s»)`  
Throw an `invalid-type` exception if *o* is not of specified *type*. *s*
is a signature string indicating the erroneous parameter, e.g.
`(assert-type o type-string "(assert-type o type s) - s")` would assert
that *s* is of `type-string`. In the case of `assert-number` the
assertion is done for `numberp`.

`(numberp «object»)` ⇒ *p*  
Return `t` if *object* is integer or double, otherwise `nil`.

`(filter «p» «l»)` ⇒ *list*  
Return a list of all elements of list *l* for which the unary predicate
function *p* evaluates not to `nil`.

`(remove «p» «l»)` ⇒ *list*  
Return a list of all elements of list *l* for which the unary predicate
function *p * evaluates to `nil`.

`(fold-left «func» «init» «list»)` ⇒ *o* <u>Ss: fold-left</u>  
Apply the binary *func*tion to *start* and the first element of *list*
and then recursively to the result of the previous invocation and the
first element of the rest of *list*. If *list* is empty return *start*.

`(flip «func»)` ⇒ *lambda* <u>f</u>  
Returns a lambda which calls binary *func* with it's two arguments
reversed (flipped).

`(reverse «l»)` ⇒ *list*  
Returns a list with all elements of *l* in reverse order

`(append [list ..][ a])` ⇒ *list'*  
Append all elements in all *list*s into a single list. If atom *a* is
present, make it a dotted list terminating with *a*.

`(apply «f» [«arg» ..][ l])` ⇒ *o*  
If *arg* is a single list call lambda *f* with all its elements as
parameters, else call *f* with all *arg*s as parameters. If list *l* is
present append all its elements to the parameter list.

`(print «o»[ «fd»])` ⇒ *o*  
`write` object *o* `:readably` to stream *fd* or output.

`(princ «o»[ «fd»])` ⇒ *o*  
`write` object *o* as is to stream *fd* or output.

`(string-to-number «string»)` ⇒ *integer*  
Converts *string* into a corresponding *integer* object. String is
interpreted as decimal based integer.

`(string-equal «s1» «s2»)` ⇒ *p*  
Returns `t` if strings *s1* and *s2* are the same, otherweise `nil`.

`(eq «a» «b»)` ⇒ *p*  
Returns `t` if *a* and *b* evaluate to the same object, number or
string, `nil` otherwise.

`(not «object»)` ⇒ *p*  
Logical inverse. In Lisp a synonym for `null`

`(length «obj»)` ⇒ *integer*  
Returns the length of *obj* if it is a string or a list, otherwise
throws a type exception.

`(memq «arg» «list»)` ⇒ *list'*  
If *arg* is contained in *list*, returns the sub list of *list* starting
with the first occurrence of *arg*, otherwise returns `nil`.

`(map «func»[ list..])` ⇒ *l* <u>Cs</u>  
Return the list of the results of applying func to each element of all
*list*s in turn.

`(nfold «f» «i» «l»)` ⇒ *o*  
“Number fold”: `left-fold`s binary function *f* on list *l* with initial
value *i*. Helper function for n-ary generic number type arithmetic.

`(fold-leftp «predicate» «start» «list»)` ⇒ *p*  
“Predicate fold”: `fold-left` binary function *predicate* to *list* with
initial value *start*. Returns `t` if *list* is empty. Helper functions
for n-ary generic number type comparison.

`(coerce ifunc dfunc x y)` ⇒ *n* <u>fd</u>  
If *x* and *y* are `type-integer` apply binary integer arithmetic
function *ifunc* to them and return the result. If any of them is
`type-double` apply binary double arithmethich function *dfunc* instead.
Helper function for n-ary generic number type arithmetic.

`(coercec «ifunc» «dfunc»)` ⇒ *lambda*` `<u>`fd`</u>  
“Coerce curry”: return a lambda `coerce`ing parameters *x* and *y* and
applying *ifunc* or *dfunc* respectively. Helper function for n-ary
generic number type arithmetic.

If the double floating point extension is loaded the following
arithmethic functions coerce their arguments to double if any of them is
double, then they use double arithmetic operators. If all arguments are
integer they use integer arthmetic. If the double extension is not
loaded, no coercion is applied and integer arithmetic operators are
used.

`(+[ «num»..])` ⇒ *n*  
Returns the sum of all *num*s or `0` if none is given.

`(*[ «num»..])` ⇒ *n*  
Returns the product of all *num*s or `1` if none given.

`(-[ «num»..])` ⇒ *n*  
Returns 0 if no *num* is given, -*num* if only one is given, *num* minus
the sum of all others otherwise.

`(/ «num»[ «div»..])` ⇒ *p*  
Returns 1/*num* if no *div* is given, *num*/*div*\[/*div*..\] if one or
more *div*s are given, With double numbers `inf` if one of the *div*s is
`0` and the sum of the signs of all operands is even, `-inf` if it is
odd. With only integers division by zero throws an exception.

`(% «num»[ «div»..])` ⇒ *n*  
Returns `1` if no *div* is given, *num*%*div*\[%*div*..\] if one or more
*div*s are given. If one of the *div*s is `0`, the program returns -nan
with double numbers. With only intergers an exception is throwsn.

`(= «num»[ «num»..])` ⇒ *p*  
`(< «num»[ «num»..])` ⇒ *p*  
`(> «num»[ «num»..])` ⇒ *p*  
`(<= «num»[ «num»..])` ⇒ *p*  
`(>= «num»[ «num»..])` ⇒ *p*  
These predicate functions apply the respective comparison operator
between all *num*s and return the respective result as `t` or `nil`. If
only one *num* is given they all return `t`.

`(min «n»[ «n»..])` ⇒ *n*  
`(max «n»[ «n»..])` ⇒ *n*  
Return the smallest/biggest number of all given *n*s.

`(or[ o..])` ⇒ *p*  
Evaluates each argument in turn, returns the first non-`nil` argument or
`nil` if there is none.

`(and[ o..])` ⇒ *p*  
Evaluates each argument in turn, returns the first `nil` argument or the
last object *o* if none evaluates to `nil. Returns` `t` if no argument
is given.

`(join «sep» «l»)` ⇒ *string* <u>f</u>  
Return a string with all elements of *l* concatenated with *sep* between
each of them.

`(fload` *stream*`)` ⇒ *0* <u>f</u>  
Reads and evaluates all Lisp objects in *stream*.

`(load` *path*`)` ⇒ *o*  
Reads and evaluates all Lisp objects in file at *path*.

`(provide «feature»)` ⇒ *feature*  
Used as the final expression of a library to register symbol *feature*
as loaded into the interpreter.

`(require «feature»)` ⇒ *feature*  
If the *feature* is not alreaded loaded, the file *feature*`.lsp` is
loaded from the library path and registers the *feature* if loading was
successful. The register is the global variable *features*.

### fLisp Extensions

Extensions are C libraries which implement additional primitives. They
are loaded into an *fLisp* interpreter after creation. Two extensions
are delivered with *fLisp*: the *file* extension for interaction with
the operating system and the *double* floating point extension.

#### String Extensions

`(char-length «string»)` ⇒ *i* <u>f</u>  
Calculates the number of bytes occupied by the first character of
*string*.

`(code-char «i»)` ⇒ *string* <u>f</u>  
Converts integer *i* into a *string* with one character, which
corresponds to the UTF-8 representation of Unicode code point *i*.

`(char-code «string»)` ⇒ *i* <u>f</u>  
Converts the first character of *string* into an integer which
corresponds to its Unicode code point.

`(strspn «string chars»)` ⇒ *i* <u>f</u>  
Returns the index of the first character in *string* which is not
contained in the string *chars*.

`(strcspn «string» «chars»)` ⇒ *i* <u>f</u>  
Returns the index of the first character in *string* which is
containted in the string *chars*.

#### POSIX Extension

This extension contains a collection of wrappers to libc functions
acording to the POSIX standards.

All functions except `getenv` are specific to fLisp.

<span class="mark">Tbd. carry over comprehensive documentation from
`file.c`</span>

`(fflush[ «stream»])` ⇒ *p*  
Flush *stream*, output or all streams

`(fseek «stream» «offset»[ «relativep»])` ⇒ *i* <u>f</u>  
Seek position *offset* in *stream* or input. If *offset* is negative
seek from end, if *relativep* is not null seek from current position, be
default seek from start. Return new position.

`(ftell[ «stream»])` ⇒ *i*  
Return current position in *stream* or input.

`(feof[ «stream»])` ⇒ *symbol*  
Return `end-of-file` if stream or input are exhausted, else `nil`

`(fgetc[ «stream»])` ⇒ *char*  
Return the next character from *stream* or input.

`(fungetc «i»[ «stream»])` ⇒ *i*  
`ungetc()` integer *i* as char to *stream* or input.

`(fgets[ «stream»])` ⇒ *string**\|*`end-of-file`  
Read a line or up to `INPUT_FMT_BUFSIZ` from *stream* or input.

`(fstat «path»[ «linkp»])` ⇒ *proplist*  
Get information about file at *path*.

`(fttyp[ «fd»])` ⇒ *p*  
Return true if input or stream *fd* is associated with a TTY.

`(fmkdir «path»[ «mode»])` ⇒ *p*  
Create directory at *path* with *mode*. Return `t` on success.

`(popen «line»[ «mode»])` ⇒ *stream*  
Run command *line* and read from/write to it.

`(pclose «stream»)` ⇒ *i*  
Close a *stream* opened with `(popen)`, return the exit status of the
command.

`(system «string»)` ⇒ *i*  
Execute *string* as command line of a system shell subpprocess,
see [system(3)](https://man7.org/linux/man-pages/man3/system.3.html) ,
returns the shell exit code as integer.

`(getenv «name»)` ⇒ *value*  
Return the value of the environment variable *name* as string. If *name*
does not exist return `nil`.

`(getcwd)` ⇒ *string*  
Return the current working directory.

`(fnmatch «pattern» «string»[ «flags»])`  
glob(7) style pattern matching. The constants: FNM_PATHNAME,
FNM_NOESCAPE and FNM_PERIOD can be binary or'd together to form the
respective *flags* value, default is 0.

[^](#toc)

#### Double Extension

All functions are specific to fLisp.

`(d+ «x» «y»)` ⇒ *z*  
Returns the sum of *x* *y*.

`(d* «x» «y»)` ⇒ *z*  
Returns the product of *x* *y*.

`(d- «x» «y»)` ⇒ *z*  
Returns *x* minus *y*.

`(d/ «arg»[ «div»..])` ⇒ *z*  
Returns *x* divided by *y*, or `inf` if *y* is zero.

`(d% «x» «y»)` ⇒ *z*  
Returns the rest (modulo) of the integer division of *x* by *y* or
`-nan` if *y* is zero.

`(d= «x» «y»)` ⇒ *p*  
`(d< «x» «y»)` ⇒ *p*  
`(d> «x» «y»)` ⇒ *p*  
`(d<= «x» «y»)` ⇒ *p*  
`(d>= «x» «y»)` ⇒ *p*  
These predicate functions apply the respective comparison operator
between *x* *y*.

[^](#toc)

### Lisp Libraries

A predefined Lisp library path is hardcoded in *fLisp*, by default it
is `/usr/local/share/flisp`. It can be overwritten at runtime with the
environment variable `FLISPLIB`. The library path is exposed to the Lisp
interpreter as the variable `script_dir`.

*fLisp* provides the following set of libraries:

core  
Integrated in the startup file, always loaded. The core library
implements a minimum set of Lisp features including code for loading
additional libraries.

flisp  
Implements some additional standard Lisp functions.

string  
String manipulation library.

file  
File, filename and directory operations.

#### flisp Library

`(listp «o»)` ⇒ *p* <u>D</u>  
Returns true if *o* is `nil` or a *cons*.

`(nthcdr «i» «l»)` ⇒ *l'*  
Return sub list of *l* starting from zero-based *i*th element to the
last.

`(nth «i» «l»)` ⇒ *o*  
Return zero-based *i*th element of list *l*

`(fold-right «f» «o» «l»)` ⇒ *o'* <u>Cs</u>  
Apply binary function *f* to last element of *l* and *o*, then
recursively to the previous element and the result.

`(unfold «f» «o» «p»)` ⇒ *l* <u>Cs</u>  
Create a list starting with *o* followed by the result of successive
application of *f* to *o* until applying *p* to the result is not `nil`
anymore.

`(iota «count»[ «start»[ «step»]])` ⇒ *l* <u>Cs</u>  
Create a list of *count* numbers starting with *start* or `0` if not
given by successively adding *step* or `1` if not given.

`(prop-get «l» «k»)` ⇒ *o*  
Return value of key *k* from property list *l*.

`(atom «o»)` ⇒ *p*  
`t` if *o* is not a *cons*.

`(zerop «x»)` ⇒ *p*  
`t` if number *x* is zero.

`(equal «o1» «o2»)` ⇒ *p*  
Return `nil` if *o1* and *o2* are not isomorphic.

[^](#toc)

#### String Library

`(string-trim-front «s»)` ⇒ *s'*  
Remove all space characters at the start of *s*.

`(string-trim-back «s»)` ⇒ *s'*  
Remove all space characters at the end of *s*.

`(string-trim «s»)` ⇒ *s'*  
Remove all space characters from start and end of *s*.

`(string-ref «s» «r»)` ⇒ *c*  
Return a string with only the character at position *r* of *s*.

`(string-startswith «s» «search»)` ⇒ *p*  
Return `t` if the first characters of *s* are the same as *search*,
`nil` otherwise.

`(string-shrink-right «s»)` ⇒ *s'*  
Return a copy of string *s* with the first character removed.

`(string-shrink-left «s»)` ⇒ *s'*  
Return a copy of string *s* with the last character removed.

`(string-first-char «s»)` ⇒ *s'*  
Return a string with the first character of *s*.

`(string-last-char «s»)` ⇒ *s'*  
Return a string with the last character of *s*.

`(string-empty-p «s»)` ⇒ *p*  
Returns `t` if *s* is the empty string, `nil` otherwise.

`(string-split «sep» «s») ⇒ l` <u>f</u>  
Return a list with all sub strings of string *s* which are separated by
the string *sep* or *s* if *sep* is not contained. If *sep* is the empty
string return a list with all characters of *s*.

[^](#toc)

#### File Library

`(mkdir «s»[ «parent-p»])` ⇒ *created-p*  
If *parent-p* is `nil` or absent call `(fmkdir «s»)`, otherwise create
all missing parent directories and finally directory *s*. Return `t` if
directory already exists, or if *s* is one of `/`, `.`, `..` or the
empty string. Return `nil` if directory was created successfully. Throws
exceptions if  *s* cannot be created.

`(file-name-directory «s»)` ⇒ s <u>e</u>  
Return the directory part of path *s* with a trailing `/` or `nil` if
*s* does not have a directory part.

`(file-name-nondirectory «s»)` ⇒ s <u>e</u>  
Return the last segment of path *s*.

`(file-name-extension «s»)` ⇒ *s* <u>e</u>  
Return the extension of path *s*, or nil if there is none.

[^](#toc)

### `flisp` Command Line Interpreter

The command line interpreter `flisp` reads a single Lisp file on
startup. The default path of this <span class="dfn">startup file</span>
is hardcoded in the binary but can be overwritten at runtime with the
environment variable `FLISPRC`. The default path
is `/usr/local/share/flisp/init.lsp`. but can be modified at compile
time.

The installed startup file includes the `core.lsp` library. This allows
to load Lisp files from the library with the  `require` function.

[^](#toc)
