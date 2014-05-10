COMPILER = g++
COMPILER_FLAGS = -g -std=c++11
LINKER = g++
LIBS = -lpthread

all: user server 

user: user.cpp
	$(COMPILER) user.cpp $(COMPILER_FLAGS) -o user `pkg-config --cflags --libs gtk+-2.0 gstreamer-video-1.0 gstreamer-1.0` $(LIBS) 

server:server.cpp
	 $(COMPILER) server.cpp $(COMPILER_FLAGS) -o server -pthread

chat:
	./user --sync 

clean:
	rm -rf *o user server 

