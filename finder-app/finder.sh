#!/usr/bin/bash

if [ $# -ne 2 ]
then
	echo "Usage: $0 <filesdir> <searchstr>"
	exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d $filesdir ]
then
	echo "Usage: $0 <filesdir> <searchstr>"
	echo "<filesdir> needs to be a directory"
	exit 1
fi

number_files=`find $filesdir -type f | wc -l`
number_matching_lines=`find $filesdir -type f | xargs grep $searchstr | wc -l`

echo "The number of files are $number_files  and the number of matching lines are $number_matching_lines"

