#!/bin/sh
exec >&2
redo-ifchange redo.c
# dietlibc knows 'dprintf' as 'fdprintf'
diet gcc -pipe -g -Os -Wall -Wextra -Wwrite-strings -Ddprintf=fdprintf -o $3 $1.c
