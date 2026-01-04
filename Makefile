CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

SRC = server.cpp http.cpp storage.cpp

all: server

server: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o server

tests/run_tests: tests/test_core.cpp http.cpp storage.cpp
	$(CXX) $(CXXFLAGS) tests/test_core.cpp http.cpp storage.cpp -o tests/run_tests

test: tests/run_tests
	./tests/run_tests

.PHONY: clean test

clean:
	rm -f server tests/run_tests
	rm -rf test_data
