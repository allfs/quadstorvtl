#!/bin/bash
set -x

cd ~
mkdir -p RPMS
cd RPMS
mkdir -p BUILD  RPMS  SOURCES  SPECS  SRPMS
cd ~
echo "%_topdir $PWD/RPMS" > ~/.rpmmacros
