CC := gcc
CFLAGS := -Wall -Wextra -pthread

SRCDIR := source
INCDIR := libs

all: server client

server: server.o markdown.o
	$(CC) $(CFLAGS) -o $@ $^

client: client.o markdown.o
	$(CC) $(CFLAGS) -o $@ $^

markdown.o: $(SRCDIR)/markdown.c $(SRCDIR)/document.c $(SRCDIR)/command.c
	$(CC) $(CFLAGS) -r -o $@ $^

%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o server client doc.md

.PHONY: all clean