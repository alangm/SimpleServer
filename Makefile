all: compile

compile:
	g++ -std=c++11 -lpthread -o server server.cpp

clean:
	rm server

