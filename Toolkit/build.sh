#!/bin/sh
#
# The ExaHyPE toolkit compilation script wrapper.
#

# Make sure the script can be called from anywhere.
cd "$(dirname "$0")"

# a small check to ensure you have java installed.
isinstalled() { which $1 2>&1 >/dev/null; }

for dep in java javac jar; do
	if ! isinstalled $dep; then
		echo "ExaHyPE toolkit mini build system check failed:"
		echo "Could not find '$dep' on your PATH (PATH=$PATH)."
		echo "Probably you have to install the Java compiler (JRE) on your machine or load further modules."
		exit 1
	fi
done

# stop on errors
set -e

cd src
make clean
make all

