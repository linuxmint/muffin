#!/bin/sh

inputfile="$1"
outputfile="$2"

sed -n -e 's/^ \{1,\}ADD_TEST *( *\([a-zA-Z0-9_]\{1,\}\).*/\1/p' "$inputfile" | while read -r test; do
  echo "$test" >> "$outputfile"
done
