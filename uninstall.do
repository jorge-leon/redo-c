#!/bin/sh
DESTDIR="$(cat DESTDIR)"
set -x
for t in $(cat TARGETS); do rm "$DESTDIR/$t"; done
