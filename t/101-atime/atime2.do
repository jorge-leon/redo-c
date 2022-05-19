# make sure redo doesn't think merely *reading* the old file counts as
# modifying it in-place.
redo-ifchange ../py
cat $1 >/dev/null 2>/dev/null || true
../py tick.py
cat $1 >/dev/null 2>/dev/null || true
echo hello
