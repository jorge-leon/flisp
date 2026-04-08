# fLisp

fLisp is a tiny yet practical interpreter for a dialect of the Lisp
programming language. It is designed to be embeddable in applications
as extension language.

fLisp is embedded into the
[Femto](https://github.com/jorge-leon/femto) editor.

> A designer knows he has achieved perfection not when there is
> nothing left to add, but when there is nothing left to take away.
> -- <cite>Antoine de Saint-Exupery</cite>


## Why the name fLisp

Lower case "f" stands for the femto metric prefix which represents
10^-15 — a very small number.  fLisp is meant to be very small.

Since "femtolisp" is already taken by a not so "femto" Lisp
interpreter, the choice fell on "fLisp".


## Goals of fLisp

- To be the smallest embeddable Lisp interpreter yet powerful enough
  to drive real world applications.
- To be rock stable, predictable and consistent.
- To consume as little resources as possible.
- To be easy to understand without extensive study [to encourage further
  experimentation].

Size by version:

	Version	Library	Binary	C-Lines/sloc/Files	Lisp-Lines/sloc/Files
	0.13	 ?		 85584	 3.6k/2.4k/6			373/272/3
	0.14     ?		 82984   3.7k/2.4k/7			610/355/5
	0.15     67096	 87336   3.8k/2.5k/7            358/166/5
	0.16    223948  241696   3882/2547/7            371/166/5
	0.17α   305512  324224   4569/3188/10           630/369/5

Binary sizes are measured on Debian GNU/Linux amd64. The Library
column shows the size of the `lispflisp.a` embeddable library, the
Binary column the size of the `flisp` command line interpreter which
contains the string and the posix library.

## Building

fLisp depends only on the standard C libraries.

The default Makefile target:

	make all

creates:
- fl .. The flisp micro repl command line utility.
- flisp .. An fLisp Repl written in fLisp.
- libflisp.a, flisp.pc  .. The library and pkg-confif description file
  for embedding fLisp in other applications.

	make install

Installs fl, the command line utility flisp, documentation and Lisp
libraries.

	make install-dev

Installs the libraries, header files and pkg-config files.

	make deb

Builds the following Debian packages:

- `flisp` .. command line utilities.
- `flisp-common` .. Lisp libraries
- `flisp-doc` .. POSHdoc/HTML and Markdown documentation.
- `flisp-dev` ..  Development resources.

# Documentation

Extensive documentation is available in the `doc` directory in POSHdoc/HTML and Markdown format.

- [Manual](doc/flisp.html) ([Markdown](doc/flisp.md)).
- [Development](doc/develop.html) of and with fLisp ([Markdown](doc/develop.md)).
- [Implementation](doc/implementation.html) Details ([Markdown](doc/implementation.md)).
- [History](doc/history.html) Details ([Markdown](doc/history.md)).
