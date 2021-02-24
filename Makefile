CFLAGS = -Wall -Werror -pthread -O3 -s

# Old compiler might need this
# CFLAGS += -std=gnu11

pace: pace.c

.PHONY: clean
clean:; rm -f pace
