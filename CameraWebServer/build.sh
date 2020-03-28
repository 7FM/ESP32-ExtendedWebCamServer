#!/bin/sh
#docker run --rm -v $PWD:/project -w /project espressif/idf:release-v4.0 idf.py build
docker run --rm -v $PWD:/project -w /project espressif/idf:latest idf.py build
