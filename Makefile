CXX = g++
CXXFLAGS = -Wall -O0 -g -std=c++17 -pthread -MMD -MP

TARGET = server
SRCS = main.cpp error.cpp net.cpp thread_main.cpp unpthread.cpp room.cpp signal.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)
