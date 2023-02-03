CC = gcc
PARAMS = -std=gnu99 -Wall -Wextra -Werror -pedantic	-pthread
all:
	${CC} ${PARAMS} proj2.c -o proj2
