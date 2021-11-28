#!/bin/sh
exec >&2
for t in $(cat TARGETS); do
    [ -e $t ] || {
	( PATH=.:$PATH; ./redo; PATH=${PATH#.:} ); break
    }
done
set -x
cp -a $(cat TARGETS SCRIPTS) $(cat DESTDIR)
