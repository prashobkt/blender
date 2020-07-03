#!/bin/sh
set -e
if [ ! -f /var/lib/buildbot/.ssh/known_hosts ]
then
    ssh-keyscan download.blender.org > /var/lib/buildbot/.ssh/known_hosts
fi
scp $1 jeroen@download.blender.org:$2