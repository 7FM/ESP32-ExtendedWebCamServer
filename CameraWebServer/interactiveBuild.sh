#!/bin/sh
#docker run --rm -v $PWD:/project -w /project -it espressif/idf:release-v4.0
docker run --rm -v $PWD:/project -w /project -it espressif/idf:latest
