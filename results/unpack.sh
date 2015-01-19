#!/usr/bin/env bash

if [ $# -ne 1 ]; then
    echo "usage: $0 archive">&2
    exit 1
fi

echo "using archive $1"

DEFAULT='f-0.2-k-1.tgz'
tar zxf "$DEFAULT"

tar zxf "$1"
