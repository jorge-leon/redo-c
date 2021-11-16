#!/bin/sh
rm -f *.pdf
for PART in front content annex; do
    enscript -o $PART.ps input/$PART.txt
    ps2pdf $PART.ps
    [ "$PART" = content ] && rm $PART.pdf || rm $PART.ps
done

