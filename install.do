#!/bin/sh
exec >&2
set -x
cp -a $(cat TARGETS SCRIPTS) $(cat DESTDIR)
