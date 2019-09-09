cc=g++ -std=c++0x
requestData:requestData.o epoll.o util.o
	$(cc) -o requestData.o epoll.o util.o
requestData.o: requestData.h epoll.h util.h requestData.cpp
	$(cc) -c requestData.cpp
.PHONY: clean
clean:
	-rm requestData *.o

