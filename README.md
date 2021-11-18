--
Title: redo - Build Target Files from Recipes
Author: Leah Neukirchen <leah@vuxu.org>
--

# Introduction

All sort of workflows start by preparing one ore more files and
process them into one or more output files, often over several steps.
Examples are: processing raw image data or media into different
finished output products, building and shipping software, or
processing bodies of text and media into websites, books or other
documents.

These workflows are often repetitive: input files are modified or
replaced by new ones and have to be processed again to produced the
modified output.

Different tools have been developed to automate this type of
re-processing of modified *sources* into *target* assets; *redo* is
one of them.


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

Instead of writing the target to the $3 file you can also print it to
standard output.


# Dependencies

A target file only needs to be built again, when it's sources change,
or when the build process changes.  *redo* will take care by itself
for the latter, but you need to provide information about the sources
in the 'do' script of a target. This is done with the command
`redo-ifchange`.

This command expects a list of source files as argument, which are
registered as dependencies of the target file.  If a source file is a
*redo* target itself (there is a `target.do` file for it) then *redo*
builds the target (and its dependencies if needed).

If any of the sources has changed since the last run *redo* knows that
it has to build the current target again.

A second command for maintaining dependencies between targets is
provided: `redo-ifcreate`.  It takes a list of files which do not yet
exist and registers them as dependencies for the current target.

# A Simple Example

Suppose we have to create a book as PDF format out of a front page
PDF, a content Postscript file and a PDF annex file.  We use `pdf2ps`
to convert the Postscript file to PDF format and `pdfunit` to merge
the PDF files.

	ps2pdf content.ps  # creates content.pdf
	pdfunite front.pdf content.pdf annex.pdf book.pdf
	
Let's create some 'do' scripts to automate this:

`content.pdf.do`:
	#!/bin/sh
	ps2pdf content.ps -  # - write the target to stdout

`book.pdf.do`:
	#!/bin/sh
	SOURCES="front.pdf content.pdf annex.pdf"
	redo-ifchange $SOURCES
	pdfunite $SOURCES $3  # the last argument to pdfunit is the output file

When we run `redo-ifchange book.pdf` for the first time, we get the
following output:

	redo book.pdf # ./book.pdf.do
	redo   content.pdf # ./content.pdf.do

and two files: `book.pdf` and `content.pdf`.

When we run it again, we get no output since nothing has changed.

When we replace e.g. `annex.pdf` with an arbitrary PDF file we get:

	redo book.pdf # ./book.pdf.do

and a new `book.pdf`. The `content.pdf` file is not rebuilt because
`content.ps` has not changed.


# Caveats and Conveniences

## Default 'all' Recipe

It is common to have an `all.do` recipe which `redo-ifchange`s all
targets.  Running `redo` without arguments runs itself as `redo all`.

Don't have a target file name `all`.


## Default Recipes for Specific Extensions

'default do' files with match all targest with a specific extension
can be created. E.g.: to process all `.tex` files with one recipe
write a `default.tex.do` file.  'do' files for specific targets are
override 'default' recipes. `target.tex` would be processed by
`target.tex.do` instead of `default.tex.do`.

You can create `default.*.do` files in a top level directories and run
`redo` in a subdirectory.  All parent directories up to `/` are
checked for default recipes.  Any 'do' file is always executed in its
directory and it's parameters are specified relative to this
directory. Take into account that the $1, $2 and $3 might be in a
different directory when you write a recipe.

Don't have a target file name `default.ext` if you have a 'do' script
named `default.ext.do`.



## Changing the Recipe Interpreter

To use a different interpreter for a 'do' file make it executable and
set a corresponding shebang line, e.g.: `#!/usr/bin/env perl`.  It is
good practice to put `#!/bin/sh` into non-executable 'do' files
anyway.


## redo Always re-builds its Target

Use `redo-ifchange` if you want to rebuild only when dependencies have
changed.


## Other

Builds can be started from every directory and should yield same results.

When a target makes no output, no target file is created.  The target
is considered always out of date. (Note: seems not to work with empty
stdout).


# 'do' Details

* `redo -x` traces execution of non executable 'do' files by adding
  `-x` to the `/bin/sh` invocation.

* `redo -S` (or `REDO_STDOUT=0)` does not capture stdout into the target.

* Parallel builds can be started with `redo -j N` (or `JOBS=N redo`).

* `redo -f` will consider all targets outdated and force a rebuild.

* `redo -k` will keep going if a target failed to build.

* `.do` files always are executed in their directory, arguments are
  relative paths.

* To detect whether a file has changed, we first compare `ctime` and
  in case it differs, a SHA2 hash of the contents.

* Housekeeping is done in files `.dep.BASENAME`, `.depend.xxx`,
`.target.xxx`, and `.BASENAME.lock` all over the tree. 


# Install

You need to have dietlibc installed for default compilation.  This
gives you a <100k static `redo` binary.

Get the `redo-c` sourcecode, set PATH to include the `diet` binary,
`cd` into the 'redo-c' top level directory and run `./bootstrap.sh`.

This builds the `redo` binary and makes the builtin `redo-*` by
symlinking to it.

The `setup.do` target installs 'redo-c' into `/usr/local/bin`.  The
first time you run it as `./redo setup`.

Remove 'redo-c' from `/usr/local/bin` with the 'uninstall` target.


# References

This software is a slightly modified version of redo-c by Leah
Neukirchen which originally was perceived by Daniel J. Bernstein.


- README.orig.md from Leah Neukirchen
- [redo in Python](https://redo.readthedocs.io/en/latest/) by Appenwarr
- [Tutorial by Jonathan de Boyne Pollard](http://jdebp.info/FGA/introduction-to-redo.html)
- [Daniel J. Bernstein redo page](http://cr.yp.to/redo.html)
- [dietlibc](https://www.fefe.de/dietlibc/)


## Copying

To the extent possible under law, Leah Neukirchen <leah@vuxu.org>
has waived all copyright and related or neighboring rights to this work.

http://creativecommons.org/publicdomain/zero/1.0/
