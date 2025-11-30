#!/usr/bin/env bash

if [ $# -ne 2 ]
then
    echo two arguments needed: filesdir and searchstr >&2
    exit 1
fi

filesdir="$1"
searchstr="$2"

if [ ! -d "$filesdir" ]
then
    echo "$filesdir is not a directory" >&2
    exit 1
fi

echo filesdir="\"$filesdir\"" searchstr="\"$searchstr\""

X=$(find "$filesdir" -type f | wc -l)

Y=0
while IFS= read -r -d $'\0' filename;
do
    nlines=$(grep "$searchstr" "$filename" | wc -l)
    Y=$((Y+nlines))
done < <(find "$filesdir" -type f -print0)

message="The number of files are $X and the number of matching lines are $Y"

printf "$message\n"
