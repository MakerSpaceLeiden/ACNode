#!/bin/sh
if [ $# != 1 ]; then
	echo "Syntax: $0 <version>"
	exit 1
fi
V=$1

if git tag -v "$V" 2> /dev/null | grep -q $V; then
	echo "Already cut a release at that version - increase the number first."
	exit 3
fi

if ! grep -q "^version=$V" library.properties; then
	echo "Update library.properties first"
	exit 2
fi

if ! git status | grep -q 'nothing to commit, working tree clean'; then
	echo "Commit any changes first."
	exit 3
fi

git tag -a $V -m "Release tag; $V"
git push origin $V

echo Now log onto github and create a release for this tag
echo https://github.com/MakerSpaceLeiden/ACNode/releases

