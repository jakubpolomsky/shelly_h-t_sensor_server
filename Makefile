CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
LDLIBS = -lcurl

SRC = server.cpp http.cpp storage.cpp storage_json.cpp

all: server

server: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) $(LDLIBS) -o server

tests/run_tests: tests/test_core.cpp http.cpp storage.cpp storage_json.cpp
	$(CXX) $(CXXFLAGS) tests/test_core.cpp http.cpp storage.cpp storage_json.cpp $(LDLIBS) -o tests/run_tests

tests/run_integration: tests/test_integration.cpp
	$(CXX) $(CXXFLAGS) tests/test_integration.cpp $(LDLIBS) -o tests/run_integration

test: tests/run_tests tests/run_integration
	./tests/run_tests
	./tests/run_integration

.PHONY: clean test

clean:
	rm -f server tests/run_tests
	rm -rf test_data
