CXX = g++
CXXFLAGS = -Wall -O0 -g -std=c++11 -pthread   

TARGET = server
SRCS = main.cpp error.cpp net.cpp thread_main.cpp unpthread.cpp room.cpp signal.cpp
OBJS = $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)  