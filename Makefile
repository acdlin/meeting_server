CXX = g++
CXXFLAGS = -Wall -g -std=c++17

TARGET = server
SRCS = main.cpp error.cpp net.cpp server.cpp
OBJS = $(SRCS:.cpp=.o)

$(TARGET) : $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

main.o : main.cpp error.h server.h net.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

net.o : net.cpp net.h
	$(CXX) $(CXXFLAGS) -c net.cpp -o net.o

server.o : server.cpp error.h 
	$(CXX) $(CXXFLAGS) -c server.cpp -o server.o

error.o : error.cpp error.h
	$(CXX) $(CXXFLAGS) -c error.cpp -o error.o


.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)