.PHONY: all clean run

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
LDFLAGS := -pthread

TARGET := Web.exe
SOURCES := main.cpp
HEADERS := HTTPServer.hpp HTTP.hpp TCPServer.hpp Router.hpp HttpDetail.hpp ThreadPool.hpp ServerConfig.hpp Routes.hpp

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  make        - Build the project"
	@echo "  make run    - Build and run the server"
	@echo "  make clean  - Clean build artifacts"

