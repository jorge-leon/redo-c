#!/bin/sh
exec >&2
which redo || [ -x ./redo ] && PATH=.:$PATH
redo $([ -e redo ] || echo redo) links
PATH=${PATH#.:}

