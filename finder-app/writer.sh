#!/usr/bin/env bash

if [ $# -ne 2 ]
then
    echo "Usage: <file path> <text string>"
    exit 1
fi

file_path="$1"
writestr="$2"

file=$(basename "$file_path")		# "${file_path%/*}"
path=$(dirname "$file_path")		# "${file_path##*/}"

mkdir -p "$path"

echo "$writestr" > "$path/$file"

if [ $? -ne 0 ]
then
    echo "Error writing file \"$path/$file\"" >&2
    exit 1
fi
