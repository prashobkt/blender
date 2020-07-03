Buildbot-lts
============

This folder contains configuration files and build scripts for making a release.
It originated when we started to do LTS releases what happens once every two
weeks. The idea is to automate the manual steps where possible and provide a
pipeline process for the manual steps that still needs to happen.

By using the same software as builder.blender.org it would already put us in
the right direction when we might want to make this part of builder.blender.org
for now it is running on a VM in the blender institute as it contains user
private keys and the process needs to be controlled security wise.

But of course the source and configurations are public available for anyone to 
check, develop and use.

Setting up build-bot
--------------------

instructions from https://github.com/cjolowicz/docker-buildbot.

Create custom buildbot worker containing packages we need for building (git, wget).

    cd docker
    docker build -t buildbot --no-cache .
    cd worker
    docker build -t buildbot-worker --no-cache .

    docker network create net-buildbot

    docker rm lts-buildbot && docker run --init --name lts-buildbot --network net-buildbot --publish=127.0.0.1:8010:8010 -t -i  buildbot

    docker rm lts-worker && docker run --init --name lts-worker --network net-buildbot --name lts-worker -e BUILDMASTER=lts-buildbot -e WORKERNAME=lts-worker -e WORKERPASS=secret -t -i  buildbot-worker

