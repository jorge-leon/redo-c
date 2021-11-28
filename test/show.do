#!/bin/sh
exec >&2
echo $0\[$$\]: $1 $2 \# $PPID
redo c
