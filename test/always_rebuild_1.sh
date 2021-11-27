#!/bin/sh -eu
# A target built with a dofile that invoked redo-always must always be rebuilt.

>a.do cat <<EOF
>c printf '1\n'; redo-ifchange b; cat b
>c printf '2\n'; redo-ifchange b; cat b
>c printf '3\n'; redo-ifchange b; cat b
EOF

>b.do cat <<EOF
redo-always
cat c
EOF

redo a

exec 9< a
<&9 read -r number_a1
<&9 read -r number_a2
<&9 read -r number_a3
exec 9<&-

test 1 -eq ${number_a1}
test 2 -eq ${number_a2}
test 3 -eq ${number_a3}
