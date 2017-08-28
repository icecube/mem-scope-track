all:
	g++ -fPIC test.cxx -shared -o libTest.so
	g++ normal.cxx -L. -l Test
	g++ -fPIC inject.cxx  -shared -o libInject.so

test:
	./a.out
	LD_PRELOAD=./libInject.so ./a.out
