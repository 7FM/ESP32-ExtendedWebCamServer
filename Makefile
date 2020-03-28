srcs = $(wildcard *.cpp)
objs = $(srcs:.cpp=.o)
deps = $(srcs:.cpp=.d)

CC=g++
CCFLAGS=-Wall
LDFLAGS=-lboost_iostreams -lz

.PHONY: all clean updateCameraIndex build

all: updateCameraIndex build

clean:
	rm -f $(objs) $(deps) create_website_helper
	. "./env/bin/activate" && platformio run --target clean
	rm -rf env

create_website_helper: $(objs)
	$(CC) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CCFLAGS) $(DBG_FLAGS) -MMD -MP -c $< -o $@


index_ov2640.html: create_website_helper
	if [ ! -f $@ ]; then ./create_website_helper 42 ; fi

index_ov3660.html: create_website_helper
	if [ ! -f $@ ]; then ./create_website_helper 42 ;	fi

updateCameraIndex: create_website_helper index_ov3660.html index_ov2640.html
	./create_website_helper

build:
	cd CameraWebServer && ./build.sh


-include $(deps)
