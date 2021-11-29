# ***redo - Build Target Files from Recipes***

# Introduction

Write down instructions how to build file `target` from files `a`,
`b`, `c`, ... in the `target.do` file. `redo target` will replay these
instructions, but only if `a`, `b`, `c`, or `target.do` - the build
recipe - change.

Write `a.do` instructions to build `a` from `x`, and `redo target`
will replay `a.do`, then `target.do` if `x` changes.

Use *redo* to create reliable, sofisticated workflow chains.

This README file explains *redo* basics first, then how to get *redo*
and finally gives credits and lists further sources of information.


# Recipes

In *redo* you create a 'do' script for each target file to be created
in a certain directory; in order to create file `target` you have to
create a 'do' script named `target.do`.  The 'do' script contains
instructions how to build the file `target` out of a (possibly empty)
set of source files, which do not necessarily reside in the same
directory.

'do' scripts are by default executed by the shell (`/bin/sh`); you can
write all build instructions just like you would do on the
commandline.

In order to build the file `target` you run:

	redo target
	
This will execute the instructions in `target.do`.

You can build several target files at the same time by specifying all
of them as arguments on the commandline.

'do' scripts receive three parameters from redo:
- $1 .. the target file name
- $2 .. the target file name without extensions.
- $3 .. a temporary file name: write the build product into this file

If the 'do' script succeeds (exits with 0), the temporary file is
moved to the target file name.

Instead of writing the target to the $3 file you can also create it by
writing its content to the standard output.


# Dependencies

A target file only needs to be built again, when it's sources change
or when the build process changes.  *redo* takes care by itself
for the latter.  Specify the list of sources via the command
`redo-ifchange *sources*..`. in the 'do' script of a target.

`redo-ifchange` registered each source as a dependency of the target
and *redo*s it if needed.


# A Simple Example

We want to create a book with PDF format out of a front page
PDF, a content Postscript file and a PDF annex file.  We use `pdf2ps`
to convert the Postscript file to PDF format and `pdfunite` to merge
the PDF files.

	ps2pdf content.ps  # creates content.pdf
	pdfunite front.pdf content.pdf annex.pdf book.pdf
	
The 'do' scripts to automate this are:

`content.pdf.do`:
	#!/bin/sh
	ps2pdf content.ps -  # - write the target to stdout

`book.pdf.do`:
	#!/bin/sh
	exec >&2
	SOURCES="front.pdf content.pdf annex.pdf"
	redo-ifchange $SOURCES
	pdfunite $SOURCES $3  # the last argument to pdfunit is the output file

When we run `redo-ifchange book.pdf` for the first time, we get the
following output:

	redo book.pdf # book.pdf.do [9937]
	redo content.pdf # content.pdf.do [9939]

and two files: `book.pdf` and `content.pdf`.

When we run it again, we get no output since nothing has changed.

When we replace e.g. `annex.pdf` with an arbitrary PDF file we get:

	redo book.pdf # book.pdf.do [16750]

and a new `book.pdf`. The `content.pdf` file is not rebuilt because
`content.ps` has not changed.


# Caveats and Conveniences

## Default 'all' Recipe

It is common to have an `all.do` recipe which `redo-ifchange`s all
targets.  Running `redo` without arguments runs itself as `redo all`.


## Empty output does not change the target

When a target makes no output, no target file is created.  A non
existing target is considered always out of date and will always be
re-done.


## Default Recipes for Specific Extensions

'default do' files which match all targest with a specific extension
can be created. E.g.: to process all `.tex` files with one recipe
write a `default.tex.do` file.  'do' files for specific targets are
override 'default' recipes. `target.tex` would be processed by
`target.tex.do` instead of `default.tex.do`.

You can create `target.do` and `default.*.do` files in a parent
directory and run `redo` in a subdirectory.  All parent directories up
to `/` are checked for default recipes.  Any 'do' file is always
executed in its directory and it's parameters are specified relative
to this directory. Take into account that the $1, $2 and $3 might be
in a different directory when you write a recipe.

# 'do' Details

* `redo -v` shows what `.do` file is used for what target.

* `redo -x` traces execution of non executable 'do' files by adding
  `-x` to the `/bin/sh` invocation.

* Parallel builds can be started with `redo -j N` (or `JOBS=N redo`).

* `redo -f` will consider all targets outdated and force a rebuild.

* `redo -k` will keep going if a target failed to build.

* `.do` files always are executed in their directory, arguments $1, $2
  and $3 are relative paths.


# Install

You need to have dietlibc installed for default compilation.  This
gives you a <100k static `redo` binary.

Get the `redo-c` sourcecode, set PATH to include the `diet` binary,
`cd` into the 'redo-c' top level directory and run `./bootstrap.sh` to
build 'redo-c'.

Install 'redo-c' into `/usr/local/bin` with `./redo install`

Remove 'redo-c' from `/usr/local/bin` with `redo uninstall`.


# References

*redo* was originally perceived by Daniel J. Bernstein.  This *redo*
implementation is derived from *redo-c* by Leah Neukirchen.

- [Daniel J. Bernstein redo page](http://cr.yp.to/redo.html)
- [redo-c](https://github.com/leahneukirchen/redo-c) from Leah
  Neukirchen on GitHub.
- [Nils Dagsson Moskopp's redo](http://news.dieweltistgarnichtso.net./bin/redo-sh.html)
- [redo in Python](https://redo.readthedocs.io/en/latest/) by Appenwarr
- [Tutorial by Jonathan de Boyne Pollard](http://jdebp.info/FGA/introduction-to-redo.html)
- [dietlibc](https://www.fefe.de/dietlibc/)

*redo* is adapted to my (leg/jorge-leon) preferences, it:
- captures stdout of do files in the target file,
- does not print progress on stderr.  Use `redo -v` to see it working,
- does not create an empty target if $3 is empty. This allows for
  "phony" targets and protects against silly mistakes.  Truncate
  targets explicitely if needed,
- creates the temporary output file $3 with the same permissions of an
  existing target, or with 0644 - subject to the umask.
- searches for `target.do` before `default.do` - also in parent
  directories,
- is modified for compilation with dietlibc.
- has a `clean`/`install`/`uninstall` and even a `mrproper.do` file
- uses the shorter and faster SipHash-2-4-64 instead of sha256
- has a dependency cycle detection.


## Copying

To the extent possible under law, Leah Neukirchen <leah@vuxu.org>
has waived all copyright and related or neighboring rights to this
work and so do I.

http://creativecommons.org/publicdomain/zero/1.0/
