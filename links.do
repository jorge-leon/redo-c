#!/bin/sh
exec >&2
LINKS="redo-always redo-hash redo-ifchange redo-ifcreate"
for l in $LINKS; do echo $l.links.to.redo; done | xargs redo
