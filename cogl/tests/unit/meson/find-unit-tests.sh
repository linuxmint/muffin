#!/bin/sh

inputdir="$1"
outputfile="$2"

echo > "$outputfile"

grep -h -r --include \*.c UNIT_TEST "$inputdir" | \
    sed -n -e 's/^UNIT_TEST *( *\([a-zA-Z0-9_]\{1,\}\).*/\1/p' > "$outputfile"
