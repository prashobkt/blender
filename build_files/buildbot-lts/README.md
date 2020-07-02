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
