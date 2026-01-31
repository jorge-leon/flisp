# Development of and with *fLisp*

### Introduction

This document explains how to extend and embed *fLisp* into an
application.

Other documentation topics:

- [*fLisp* Manual](flisp.html) [(Markdown)](flisp.md)
- [History](history.html) ([Markdown](history.md))
- [Implementation](implementation.html) Details
  ([Markdown](markdown.md))

### Table of Contents

1.  [Introduction](#introduction)
2.  Table of Contents
3.  [Embedding Overview](#embed_overview)
4.  [fLisp C Interface](#c_api)
5.  [Building Extensions](#extensions)

### Embedding Overview

*fLisp* can be embedded into a C application. Two examples of embedding
are the [Femto text editor](https://github.com/jorge-leon/femto)  and
the `flisp` command line Lisp interpreter. This provides access from the
application to the Lisp language features.

In order to get access from the Lisp interpreter to the application
extensions to *fLisp* have to be implemented. These consist in C
function with a specific signature and which must return a Lisp
object. C macros facilitate introspection and access to the arguments.
The C functions are registered with an *fLisp* interpreter with the
`flisp_register_primitive()` function.

Two extensions are already provided with *fLisp*. The *file* extension
gives access to the operating system features, exposing some POSIX
functions to *fLisp*. If floating point arithmetic is required the
*double* extension can be used. The extensions are described in the
*fLisp* manual.

Operating the *fLisp* interpreter in an application involves the
following steps:

1.  Creation of an *fLisp* interpreter with `flisp_new()`.
2.  Optional loading of extensions into the interpreter.
3.  Optional loading of Lisp libraries into the interpreter.
4.  Evaluation/execution of Lisp commands with `flisp_eval()`.

Different flows of operation can be implemented. For example the Femto
editor initializes the interpreter without input/output file descriptors
and sends strings of Lisp commands to the interpreter, either when a key
is pressed or upon explicit request via the editor user interface.
Another example is the `flisp` command line interpreter which sets
`stdout` as the default output file descriptors of the *fLisp*
interpreter and loads a startup script which implements a read eval
print loop, reading from `stdin` - thus creating it's own user
interface.

Lisp commands (or programs) are handed over to the `flisp_eval()`
function as strings. The input is processed within
a [`catch`](interp_ops) command and the result stored in its internal
structure. C macros are provided for easy access to the result.

[^](#toc)

### fLisp C Interface

*fLisp* exposes the following public interface functions:

`flisp_new()`  
Create a new interpreter.

`flisp_destroy()`  
Destroy an interpreter, releasing resources.

`flisp_eval()`  
Evaluate a string or the input stream until exhausted or error.

`flisp_write_object()`  
Format and write object to file descriptor.

`flisp_write_error()`  
Format and write the error object and error message of an interpreter to
a file descriptor.

`flisp_register_constant()`  
Register a global symbol binding.

`flisp_register_primitive()`  
Register an extension *fLisp* function.

#### Interpreter Creation

A new *fLisp* interpreter is created with the function `flisp_new()`.
Upon success a pointer to an initialized *Interpreter* structure is
returned, which is required for all other *fLisp* operations. If the
required memory for the interpreter cannot be allocated a `NULL` pointer
is returned instead.

`flisp_new()` registers the interpreter in a circular single linked list
stored in the global variable `flisp_interpreters`.

<span class="mark">Note: currently only creating one interpreter has
been tested.</span>

The signature of `flisp_new()` is:

> 
>     Interpreter *flisp_new(site_t size, char **argv, char *library_path, FILE *input, FILE *output, FILE* debug)

*size*  
Initial memory size to allocate for the Lisp object space in bytes. If
size is zero, the Lisp interpreter will allocate memory whenever needed.

*argv*  
If not `NULL` the *fLisp* root environment is initialized with the
following symbols:

*argv0* .. bound to the string stored in `*«argv»[0]`

*argv .. *The list of strings stored in *argv*

*library_path*  
If not `NULL` the *fLisp* root environment is initialized with the
symbol *script_dir* bound to the string stored in *library_path*.

*input*

Default input stream. If *input* is `NULL` no *fLisp* input stream is
set up.

*output*

Default output stream. If *output* is `NULL` a memory stream is created
at the first invocation of the interpreter and set as the default output
stream.

*debug*

Debug output stream. If set to `NULL` no debug information is generated.

The Lisp interpreter allocates new object space in chunks of eight
kilobytes (by default) whenever a garbage collection cycle does not free
sufficient memory. This happens mainly during startup, when initial
libraries are loaded or commands are executed for the first time. It is
recommended to use the debug output to determine the final amount
required by an application and initialize the interpreter with a
corresponding *size* argument.

*argv* is meant to be set to the applications *argv* vector, however it
can be set to any other array of strings if needed.

#### Interpreter Execution

The signature of `flisp_eval()` is:

> 
>     void flisp_eval(Interpreter *interp, char *string)

If *string* is not `NULL` all Lisp expressions in *string* are evaluated
sequentually in the interpreter *interp*.

If *string* is `NULL` the  input stream of the *fLisp* interpreter
*interp* is evaluated until end of file.

If no memory can be allocated for the input string or the input file
descriptor is `NULL` no Lisp evaluation takes place and the result is a
corresponing error result.

During evaluation *fLisp* sends all output to the default output stream.
If it is set to `NULL` on initialization, output is suppressed
altogether.

After processing the input, the interpreter holds the results
corresponding to a [`catch`](interp_ops) result in its internal
structure. They can be accessed with the following C-macros:

*error_type*  
`FLISP_RESULT_CODE(interpreter)`

*message*  
`FLISP_RESULT_MESSAGE(interpreter)`

*object*  
`FLISP_RESULT_OBJECT(interpreter)`

Check for `(FLISP_RESULT_OBJECT(interpreter) != nil)` to find out if the
result is an error. Then check for
`(FLISP_RESULT_OBJECT(interpreter) == out_of_memory)` to see if a fatal
condition occured.

On error use f`lisp_write_error()` to write the standard error message
to a file descriptor of choice, or use the above C-macros and
`FLISP_ERROR_MESSAGE(interpreter)->string` for executing a specific
action. The function `flisp_write_object()` can be used to
write/serialize a result object.The signatures for these two functions
are:

`void flisp_write_object(Interpreter *«interp», FILE «*fd», Object *«object», bool readably)`  
Format *object* into a string and write it to *stream*. If *readably* is
true, the string can be read in by the interpreter and results in the
same object.

`void flisp_write_error(Interpreter *«interp», FILE «*fd»)`  
Format the error *object* and the error message of the interpreter into
a string and write it to *fd*. The *object* is written with *readably*
`true`.

#### Interpreter Destruction

<span class="mark">Note: current applications only use a single
interpreter and leave freeing of interpreter memory to the `exit()`
function. Destruction of interpreters is untested and is likely to cause
segmentation faults.</span>

`void flisp_destroy(Interpreter *«interp»)`

Frees all resources used by the interpreter.

[^](#toc)

### Building Extensions

#### Registering Primitives

An extensions has to create C functions,
called <span class="dfn">primitives</span> with the signature:

> 
>     Object *primitive(Interpreter *interp, Object **args, Object **env)

*primitive* must be a distinct name in C space. This signatures is
typedef'd to `LispEval`.

To make the primitive available to an fLisp interpreter the following
function has to be executed:

> 
>     void flisp_register_primitive(Interpreter *interp, char *name, int min_args, int max_args, Object *args_type, LispEval func)

*interp*  
Interpreter in which to register the primitive.

*name*  
Symbol name to which the primtive is bound in the root environment.

*min_args*  
Minimum number of arguments to be given to the primtive.

*max_args*  
Maximum number of arguments allowed for the primitive. If negative,
arguments must be given in tuples of the number and the number of tuples
is not restricted. `-1` means, any number of (individual) arguments.

*args_type*  
An object type symbol or `nil`. If all arguments must be of the same
single type, this type can be given here and the interpreter does type
checking. If *args_type* is set to `nil`, the interpreter does not do
type checking - the primitive has to implement type checking by itself,
see below for helper macros.

*func*  
The function pointer of the primitive to register.

[^](#toc)

#### Writing primitives

When a primitive is called from Lisp, the arguments are handed over in
the `Object **«args»` argument. This is a list of Lisp objects.

Some CPP macros are provided to simplify argument access and validation
in primitives:

`FLISP_HAS_ARGS`  
`FLISP_HAS_ARG_TWO`  
`FLISP_HAS_ARG_THREE`  
Evaluate to true if there are arguments or the respective argument is
available.

`FLISP_ARG_ONE`  
`FLISP_ARG_TWO`  
`FLISP_ARG_THREE`  
Evaluate to the respective argument.

`FLISP_CHECK_TYPE(«argument», «type», «signature»)`  
Assures that the given *argument*  (e.g. `FLISP_ARG_TWO`) is of the
given type. *type* must be a type object like `type_string`. *signature*
is the Lisp signature of the primitive followed by “` - `” and the name
of the argument to be type checked. This is used to form a standardized
`wrong-type-argument` error message.

Example:


    /* (foo integer string) => 42 - just check if the first argument is an integer and the second a string, then return the integer 42 */
    Object *foo(Interpreter *interp, Object **args, Object **env)
    {
        FLISP_CHECK_TYPE(FLISP_ARG_ONE, type_integer, "(foo integer string) - integer");
        FLISP_CHECK_TYPE(FLISP_ARG_TWO, type_string, "(foo integer string) - string");
        return newInteger(interp, 42);
    }
    …
        flisp_register_primitive(interp, "foo", 2, 2, nil, foo);
    …

Lisp objects are structs with their value as a union field. The
following object types are available:

<table>
<colgroup>
<col style="width: 20%" />
<col style="width: 20%" />
<col style="width: 20%" />
<col style="width: 20%" />
<col style="width: 20%" />
</colgroup>
<thead>
<tr>
<th scope="col">C symbol</th>
<th scope="col">Lisp type symbol</th>
<th scope="col">Type </th>
<th scope="col">Field(s)</th>
<th scope="col">Constructor</th>
</tr>
</thead>
<tbody>
<tr>
<td><code>type_integer</code></td>
<td><code>type-integer</code></td>
<td><code>int64_t</code></td>
<td><code>object-&gt;integer</code></td>
<td><code>newInteger(«interp», «integer»)</code></td>
</tr>
<tr>
<td><code>type_double</code></td>
<td><code>type-double</code></td>
<td><code>double</code></td>
<td><code>object-&gt;number</code></td>
<td><code>newDouble(«interp», «double»)</code></td>
</tr>
<tr>
<td><code>type_string</code></td>
<td><code>type-string</code></td>
<td><code>char *</code></td>
<td><code>object-&gt;string</code></td>
<td><code>newString(«interp», «string»)</code></td>
</tr>
<tr>
<td> </td>
<td> </td>
<td> </td>
<td> </td>
<td><code>newStringWithLength(«interp», «string», «length»)</code></td>
</tr>
<tr>
<td><code>type_symbol</code></td>
<td><code>type-symbol</code></td>
<td><code>char *</code></td>
<td><code>object-&gt;string</code></td>
<td><code>newSymbol(«interp», «string»)</code></td>
</tr>
<tr>
<td> </td>
<td> </td>
<td> </td>
<td> </td>
<td><code>newSymbolWithLength(«interp», «string», «length»)</code></td>
</tr>
<tr>
<td><code>type_cons</code></td>
<td><code>type-cons</code></td>
<td><code>Object *</code></td>
<td><code>object-&gt;car</code>, <code>object-&gt;cdr</code></td>
<td><code>newCons(«interp», «car», «cdr»)</code></td>
</tr>
<tr>
<td> </td>
<td> </td>
<td> </td>
<td> </td>
<td><em>car</em> and <em>cdr</em> are of type
<code>Object **</code></td>
</tr>
<tr>
<td><code>type_lambda</code></td>
<td><code>type-lambda</code></td>
<td><code>Object *</code></td>
<td><code>object-&gt;params</code>, <code>object-&gt;body</code>, <code>object-&gt;env</code></td>
<td>n.a.</td>
</tr>
<tr>
<td><code>type_macro</code></td>
<td><code>type-macro</code></td>
<td><code>Object *</code></td>
<td><code>object-&gt;params</code>, <code>object-&gt;body</code>, <code>object-&gt;env</code></td>
<td>n.a.</td>
</tr>
<tr>
<td><code>type_primitive</code></td>
<td><code>type-primitive</code></td>
<td><code>Primitive *</code></td>
<td><code>object-&gt;primitive</code></td>
<td>n.a.</td>
</tr>
<tr>
<td><code>type_stream</code></td>
<td><code>type-stream</code></td>
<td><div>
<code>Object *</code>
</div></td>
<td><code>object-&gt;path</code> (of <code>type-string</code>)</td>
<td><code>newStreamObject(«interp», «fd», «path»)</code></td>
</tr>
<tr>
<td> </td>
<td> </td>
<td><code>FILE *</code></td>
<td><code>object-&gt;fd</code></td>
<td> </td>
</tr>
<tr>
<td> </td>
<td> </td>
<td><code>size_t</code></td>
<td><code>object-&gt;len</code></td>
<td> </td>
</tr>
</tbody>
</table>

The double type is only relevant if using an fLisp interpreter with
double extension enabled.

The Lisp object constructors will eventually trigger garbage collection.
This will move all objects into a new memory region. Therefore
when creating more then one object within a primitive care has to be
taken to register the objects with the garbage collector. Registration
is started with the `GC_CHECKPOINT` macro. `GC_TRACE(«name», «value»)`
creates a traced object variable *name* of type Object \*\*, sets it to
*value* which must be of type `Object *` and registers it with the
garbage collector. The macro `GC_RELEASE` must be called to finalize the
registration. The convenience macro `GC_RETURN(«object»)` calls
`GC_RELEASE` and returns *object*.

Example:


    /* (bar) => (nil . 42) - return a cons with nil and the integer 42 */
    #if USING_GC_RELEASE
    Object *bar(Interpreter *interp, Object **args, Object **env)
    {
      GC_CHECKPOINT;
      GC_TRACE(gcAnswer, newInteger(interp, 42));
      GC_RETURN(newCons(interp, &nil, gcAnswer));
    }
    #else
    Object *bar(Interpreter *interp, Object **args, Object **env)
    {
        Object *object;
        GC_CHECKPOINT;
        GC_TRACE(gcAnswer, newInteger(interp, 42));
        object = newCons(interp, &nil, gcAnswer);
        GC_RELEASE;
        return object;
    }
    #endif
    …
        flisp_register_primitive(interp, "bar", 0, 0, nil, bar);
    …

[^](#toc)

#### fLisp Constants

It is often desirable to introduce application specific symbols. Symbols
take the role of enums or are needed to create error codes. In order to
use symbols both from C code and from Lisp, they must not be subject to
garbage collection. The *fLisp* garbage collector skips all objects
which are allocated outside of the Lisp object area and the `bind`
and `setq` oberations do not allow to set a new value for such an
object. To create such a “constant” use
`flisp_register_constant(«interp», «symbol», «value»)`. Here is an
example usage where the symbol `foobar` is registered to have the value
42.:


    Object * foobar = &(Object) { NULL, .string = "foobar" };
    Object * answer = &(Object) { NULL, .integer = 42 };
    …
        anser->type = type_integer;
        flisp_register_constant(interp, foobar, answer);
    …

The type of a statically allocated object can only be set at runtime.
`flisp_register_constant()` sets the type of the symbol to
`type_symbol`, but does not touch the type of the value. This has to be
done by the application code.

If the string length of the symbol name exceeds 11 characters the
definition of the symbol requires the following notation:


    …
    Object * very_long_symbol_name = (Object *) (&(Symbol) { NULL, .string = "very long symbol name" });​​​​​​​
    …

[^](#toc)
