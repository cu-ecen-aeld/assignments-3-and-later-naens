#!/bin/sh

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

Y=$(find "$filesdir" -type f -exec grep "$searchstr" {} + | wc -l)

message="The number of files are $X and the number of matching lines are $Y"

printf "$message\n"
