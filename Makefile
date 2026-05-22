CC = gcc
CXX = g++
CCFLAGS = -std=c99 -Wall -Wextra -Wshadow -Wconversion -Wpedantic -O2
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O2
LFLAGS =

.PHONY: all clean cppcheck

all: smoke cppcheck repl bench example example2 example3


smoke: smoke.c tinyintegerexpr.c
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)
	./$@

cppcheck: tinyintegerexpr.c tinyintegerexpr.h
	$(CXX) $(CXXFLAGS) -x c++ -c tinyintegerexpr.c -o tinyintegerexpr_cpp.o

repl: repl.o tinyintegerexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

repl-readline: repl-readline.o tinyintegerexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS) -lreadline

bench: benchmark.o tinyintegerexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example: example.o tinyintegerexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example2: example2.o tinyintegerexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

example3: example3.o tinyintegerexpr.o
	$(CC) $(CCFLAGS) -o $@ $^ $(LFLAGS)

repl-readline.o: repl.c
	$(CC) -c -DUSE_READLINE $(CCFLAGS) $< -o $@

.c.o:
	$(CC) -c $(CCFLAGS) $< -o $@

clean:
	rm -f *.o *.exe example example2 example3 bench repl smoke
