#
# Makefile
#
# Project structure:
#
# simplefs/
# -- src/
# |   -- fs.c      filesystem implementation
# |   -- fs.h      public definitions
# |   -- main.c    shell interface
#
# Output:
#
#   simplefs
#


CC     = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -O2
TARGET = simplefs
SRCS   = src/fs.c src/main.c
OBJS   = $(SRCS:.c=.o)

.PHONY: all clean run valgrind

#
# Default target
#
# Running:
#
#   make
#
# builds the filesystem program
#
all: $(TARGET)

#
# Link object files into executable
#
# Example:
#
#   fs.o + main.o
#          |
#       simplefs
#
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

#
# Compile each .c source file into .o object file
#
src/%.o: src/%.c src/fs.h
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

#
# Checks:
#
# - memory leaks
# - invalid memory access
#
# Usage:
#
#   make valgrind
#
valgrind: $(TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TARGET)


#
# Deletes:
#
# object files
# executable
# disk image
#
clean:
	rm -f src/*.o $(TARGET) disk.img
