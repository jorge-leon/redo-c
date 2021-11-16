#!/bin/sh
SOURCES="front.pdf content.pdf annex.pdf"
redo-ifchange $SOURCES
pdfunite $SOURCES $3
