#!/bin/sh

rsync -r --delete --exclude=.git --prune-empty-dirs -v _build/html/ $1
