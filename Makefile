all: ptout

ptout: main.c
	gcc -Werror -O3 -o $@ main.c

clean:
	-rm -f ptout
