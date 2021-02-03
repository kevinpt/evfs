#!/bin/sh

if [ $# -lt 1 ]; then
  echo "Usage: ${0##*/} <gh-pages path>"
  exit 1
fi

html_dir=$1


rsync -r --delete --exclude=.git --prune-empty-dirs -v _build/html/ $html_dir
