#!/bin/sh
exec >&2
[ -e redo ] || redo redo
ln -nfs redo $2
