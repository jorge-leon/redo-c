#!/bin/sh
set -x
echo $(cat TARGETS SCRIPTS) | (	cd $(cat DESTDIR) && xargs rm )

