.PHONY: all clean
all: Web.exe
clean:
	rm -f Web.exe
Web.exe:main.cpp 
	g++ -std=c++17 main.cpp -o Web.exe -pthread
