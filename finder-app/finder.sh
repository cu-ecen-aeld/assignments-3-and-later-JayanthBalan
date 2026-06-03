#!/bin/bash

# Inputs
if [ $# -ne 2 ]
then
	echo "Error: 2 Arguments Required"
	exit 1
fi

path=$1
text=$2

# Process
if [ ! -d $path ]
then
	echo "Error: Directory does not exist"
	exit 1
fi

file_count=$(find "$path" -type f | wc -l)
line_count=$(grep -r "$text" "$path"| wc -l)

# Output
echo "The number of files are $file_count and the number of matching lines are $line_count"
