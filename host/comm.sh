#!/bin/bash
cd /makestuff/apps/flcli
make deps
cd ../../hdlmake/apps
../bin/hdlmake.py -g makestuff/swled


