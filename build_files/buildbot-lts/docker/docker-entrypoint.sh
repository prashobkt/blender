#!/bin/sh

set -e

buildbot upgrade-master .

chown buildbot /var/lib/buildbot/
chown buildbot /var/lib/buildbot/.ssh/
chown -R buildbot /var/lib/buildbot/*

if [ -S /var/run/docker.sock ]
then
    group=$(stat -c '%g' /var/run/docker.sock)
else
    group=buildbot
fi

gosu buildbot:$group "$@"