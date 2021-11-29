#!/bin/sh
exec >&2
echo $0: $1 $2
printenv|grep REDO_
redo-ifchange b b3

