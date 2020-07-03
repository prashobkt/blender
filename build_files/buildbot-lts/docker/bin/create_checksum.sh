#!/bin/sh
set -e
md5sum $1 > $2.md5
sha256sum $1 > $2.sha256