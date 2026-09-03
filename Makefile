CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2
TARGETS = server client

all: $(TARGETS)

server: server.cpp common.h
	$(CXX) $(CXXFLAGS) -o server server.cpp

client: client.cpp common.h
	$(CXX) $(CXXFLAGS) -o client client.cpp

clean:
	rm -f $(TARGETS)

.PHONY: all clean
