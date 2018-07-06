#!/bin/bash

source ~/emsdk/emsdk_env.sh
export platform=emscripten
export DEBUG=1
make clean
emmake make -j3
cp *.bc ~/RetroArch/dist-scripts/
