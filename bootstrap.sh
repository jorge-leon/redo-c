#!/bin/sh
# bootstrap script

(
alias redo-ifchange=:
set -- redo redo redo
set -ex
. ./redo.do
)

./redo
