#!/bin/sh
for l in $(grep -v '^redo$' TARGETS); do
    ln -sf redo $l
done
