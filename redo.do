#!/bin/sh
exec >&2
redo-ifchange redo.c

CFLAGS="-pipe -g -Os -Wall -Wextra -Wwrite-string -Wimplicit-fallthrough=4"

which diet >/dev/null 2>&1 && {

    # dietlibc knows 'dprintf' as 'fdprintf'
    # siphash should work with  -Wimplicit-fallthrough=4, but we do not know
    # how to tell gcc
    diet gcc -Ddprintf=fdprintf -o $3 $1.c
    exit $?
}
gcc -o $3 $1.c