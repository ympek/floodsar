CC=g++
CPPFLAGS=-std=c++17
LIBS=-lgdal -lpthread

floodsar: src/main.cpp
	$(CC) -o $@ $^ $(CPPFLAGS) $(LIBS)

mapper: src/mapper.cpp
	$(CC) -o $@ $^ $(CPPFLAGS) $(LIBS)

analyze: src/analyze_dir.cpp
	$(CC) -o $@ $^ $(CPPFLAGS) $(LIBS)

testcpp: src/test.cpp
	$(CC) -o $@ $^ $(CPPFLAGS) $(LIBS)
