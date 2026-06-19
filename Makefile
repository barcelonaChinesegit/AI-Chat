HEADERS := Common.hpp Http.hpp Inet_Addr.hpp Log.hpp Mutex.hpp Session.hpp Socket.hpp TcpServer.hpp Util.hpp mysql_util.hpp

myhttp_ai: Main.cc $(HEADERS)
	g++ -o $@ Main.cc -std=c++17 -lcurl -ljsoncpp -lpthread -lmysqlclient -lssl -lcrypto
.PHONY:clean
clean:
	rm -rf myhttp_ai
