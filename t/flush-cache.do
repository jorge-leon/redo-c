redo-ifchange whichpython $1.in
read py <whichpython
(
	echo "#!$py"
	cat $1.in
) >$3
chmod a+x $3
