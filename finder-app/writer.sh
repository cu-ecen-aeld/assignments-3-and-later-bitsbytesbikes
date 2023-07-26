#!/usr/bin/bash

if [ $# -ne 2 ]
then
	echo "Usage: $0 <writefile> <writestr>"
	exit 1
fi

writefile=$1
writestr=$2

mkdir -p "$(dirname $writefile)"
echo "$writestr" > $writefile

if [ ! $? -eq 0 ]
then
	echo "Error while writing $writestr to $writefile"
	exit 1
fi
