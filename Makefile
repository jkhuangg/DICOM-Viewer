FRAMEWORKS = -framework GLUT -framework OpenGL -framework Carbon
LIBS = -lassimp -lglfw -lGLEW
OS := $(shell uname)

ifeq ($(OS),Darwin)
	LIBS += ${FRAMEWORKS}
endif

.PHONY: main

main:
	clang++ -g -O0 main.cpp tinyfiledialogs.c glad.c -o main \
	    -std=c++17 \
	    -I/opt/homebrew/include \
	    -L/opt/homebrew/lib \
	    -ldcmjpeg -ldcmdata -ldcmimgle -ldcmimage -ldcmtls -ldcmdsig -ldcmqrdb -ldcmwlm -ldcmtkcharls -lofstd \
	    -framework OpenGL \
	    -framework Cocoa \
	    -framework IOKit \
	    -framework CoreVideo \
		-framework GLUT \
	    -lglfw \
	    -lglew \
		-stdlib=libc++
