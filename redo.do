#!/bin/sh
exec >&2
redo-ifchange redo.c
# dietlibc knows 'dprintf' as 'fdprintf'
# siphash should work with  -Wimplicit-fallthrough=4, but we don't know how to tell gcc
diet gcc -pipe -g -Os -Wall  -Wextra -Wwrite-strings -Ddprintf=fdprintf -o $3 $1.c
