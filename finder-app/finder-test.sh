#!/bin/sh
# Tester script for assignment 1 and assignment 2
# Author: Siddhant Jajoo

set -e
set -u


# get FINDER
if command -v finder.sh
then
    FINDER=finder.sh
elif [ -e finder.sh ]
then
    FINDER="./finder.sh"
else
    echo "no finder"
    exit
fi

# get WRITER
if command -v writer
then
    WRITER=writer
else
    if [ -e writer.sh ]
    then
        WRITER="./writer.sh"
    else
        unset WRITER
    fi
    if [ -e writer ]
    then
        sys_arch=$(uname -m | sed s/_/-/)
        file_arch=$(file -b writer | cut -d',' -f2 | awk 'NF>0{print $NF}')
        if [ "$sys_arch" = "$file_arch" ]
        then
            WRITER="./writer"
        fi
    fi
fi
if test -z "$WRITER"
then
    echo "no writer"
    exit
fi

# get CONF
if [ -d /etc/finder-app/conf ]
then
    CONF=/etc/finder-app/conf
elif [ -d conf ]
then
    CONF=conf
elif [ -d ../conf
then
    CONF=../conf
fi

NUMFILES=10
WRITESTR=AELD_IS_FUN
WRITEDIR=/tmp/aeld-data
username=$(cat "$CONF/username.txt")

if [ $# -lt 3 ]
then
	echo "Using default value ${WRITESTR} for string to write"
	if [ $# -lt 1 ]
	then
		echo "Using default value ${NUMFILES} for number of files to write"
	else
		NUMFILES=$1
	fi	
else
	NUMFILES=$1
	WRITESTR=$2
	WRITEDIR=/tmp/aeld-data/$3
fi

MATCHSTR="The number of files are ${NUMFILES} and the number of matching lines are ${NUMFILES}"

echo "Writing ${NUMFILES} files containing string ${WRITESTR} to ${WRITEDIR}"

rm -rf "${WRITEDIR}"

# create $WRITEDIR if not assignment1
assignment=`cat "$CONF/assignment.txt"`

if [ $assignment != 'assignment1' ]
then
	mkdir -p "$WRITEDIR"

	#The WRITEDIR is in quotes because if the directory path consists of spaces, then variable substitution will consider it as multiple argument.
	#The quotes signify that the entire string in WRITEDIR is a single string.
	#This issue can also be resolved by using double square brackets i.e [[ ]] instead of using quotes.
	if [ -d "$WRITEDIR" ]
	then
		echo "$WRITEDIR created"
	else
		exit 1
	fi
fi

for i in $( seq 1 $NUMFILES)
do
    $WRITER "$WRITEDIR/${username}$i.txt" "$WRITESTR"
done

OUTPUTSTRING=$("$FINDER" "$WRITEDIR" "$WRITESTR")
echo $OUTPUTSTRING > /tmp/assignment4-result.txt

# remove temporary directories
rm -rf /tmp/aeld-data

set +e
echo ${OUTPUTSTRING} | grep "${MATCHSTR}"
if [ $? -eq 0 ]; then
	echo "success"
	exit 0
else
	echo "failed: expected  ${MATCHSTR} in ${OUTPUTSTRING} but instead found"
	exit 1
fi
