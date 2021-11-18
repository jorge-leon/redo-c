#!/bin/sh
redo all
set -x
cp -a $(cat TARGETS) $(cat DESTDIR)
