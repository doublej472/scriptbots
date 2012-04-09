all:
	c++ demo_dave.cpp -o demo_dave -L/usr/local/Cellar/boost/1.49.0/lib/ -lboost_serialization-mt
