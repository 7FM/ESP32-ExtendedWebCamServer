#!/bin/sh

#image='espressif/idf:release-v4.0'
image='espressif/idf:latest'
docker pull "$image"
docker run --rm -v $PWD:/project -w /project -it "$image"
