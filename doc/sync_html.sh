#!/bin/sh

rsync -r --delete --prune-empty-dirs -v _build/html/ $1
