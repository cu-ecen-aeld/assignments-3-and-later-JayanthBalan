#!/bin/bash

# Inputs
writefile=$1
writestr=$2

if [ $# -ne 2 ]
then
	echo "Error: Incorrect Number of Arguments"
	exit 1
fi

#Process
mkdir -p "$(dirname "$writefile")"
echo "$writestr" > "$writefile"

if [ $? -ne 0 ]
then
	echo "Error: Not able to create file"
	exit 1
fi
