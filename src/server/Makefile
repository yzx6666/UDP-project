CFLAGS+=-I../include/ -pthread
LDFLAGS+= -pthread

all: server 
server: server.o medialib.o  mytbf.o  thr_channel.o  thr_list.o
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm *.o server -rf
