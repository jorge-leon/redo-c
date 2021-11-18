#!/bin/sh
rm -f $(cat TARGETS) all install uninstall links
# leftovers
rm -f .depend.* .target.* .lock.*
