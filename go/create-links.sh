#!/bin/sh

# Parameters:

repo="$1"
dest="$2"

# Usage info:

usage=$(cat <<EOF
Usage:  ./create-links.sh <repo> <dest>

Prepares directory dest as a basis for golang remote importing of the
repository repo.

The command creates the same directory structure in dest as in repo and
creates a link to <dest>/index.html in each directory.
Only directories containing golang source files are considered.

The index.html file in the dest directory must be created by hand.
EOF
)

if [ -z "$repo" -o -z "$dest" ]; then
	echo "$usage"
	exit 0
fi

# Careful! This commands removes all sub-directories of the dest directory:
# find "$dest" -mindepth 1 -maxdepth 1 -type d | xargs rm -r

find "$repo" -mindepth 2 -type f -name '*.go' \
	| xargs -L 1 dirname \
	| sort -u \
	| sed -e "s|^$repo|.|" \
	| xargs -L 1 -I '{}' sh -c "mkdir -p {} && ln -s -r -f $dest/index.html {}"
