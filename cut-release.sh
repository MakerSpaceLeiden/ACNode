#!/bin/sh
if [ $# != 1 ]; then
	echo "Syntax: $0 <version>"
	exit 1
fi
V=$1

if ! grep -q "^version=$V" library.properties; then
	echo "Update library.properties first"
	exit 2
fi

if git tag -v "$V"; then
	echo "Already cut a release at that version - increase the number first."
	exit 3
fi
git status || exit

git tag -a $V -m "Release tag; $V"
git push origin $V
