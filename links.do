#!/bin/sh
[ -e redo ] || redo redo
for l in $(grep '^redo-.\+' TARGETS); do
    [ -L "$l" ] && [ "$(readlink $l)" = redo ] || echo $l.links.to.redo
done |
    xargs redo
