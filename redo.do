#!/bin/sh
redo-ifchange redo.c
diet gcc -pipe -g -Os -Wall -Wextra -Wwrite-strings -Ddprintf=fdprintf -o $3 $1.c
