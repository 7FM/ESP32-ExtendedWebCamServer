.PHONY: all updateCameraIndex build

all: updateCameraIndex build

updateCameraIndex:
	cd CameraWebServer/www && ./compress_pages.sh

build:
	cd CameraWebServer && ./build.sh


-include $(deps)
