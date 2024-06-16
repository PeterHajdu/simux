PREFIX ?= /usr/local
SOURCES=simux.c
EXECUTABLE=simux
CFLAGS=-Wall -Werror -pedantic -g

$(EXECUTABLE): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -lreadline -lpthread -o $(EXECUTABLE)

install: $(EXECUTABLE)
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	install $(EXECUTABLE) ${DESTDIR}${PREFIX}/bin

uninstall:
	rm ${DESTDIR}${PREFIX}/bin/$(EXECUTABLE)

all: $(EXECUTABLE)

clean:
	rm -f $(EXECUTABLE)

.PHONY: clean install all

