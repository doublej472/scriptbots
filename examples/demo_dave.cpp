/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
// demo.cpp
//
// (C) Copyright 2002-4 Robert Ramey - http://www.rrsd.com .
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#include <cstddef> // NULL
#include <iomanip>
#include <iostream>
#include <fstream>
#include <string>

#include <boost/archive/tmpdir.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/assume_abstract.hpp>

using namespace std;

/////////////////////////////////////////////////////////////
// The intent of this program is to serve as a tutorial for
// users of the serialization package.  An attempt has been made
// to illustrate most of the facilities of the package.
//
// The intent is to create an example suffciently complete to
// illustrate the usage and utility of the package while
// including a minimum of other code.
//
// This illustration models the bus system of a small city.
// This includes, multiple bus stops,  bus routes and schedules.
// There are different kinds of stops.  Bus stops in general will
// will appear on multiple routes.  A schedule will include
// muliple trips on the same route.

/////////////////////////////////////////////////////////////
// gps coordinate
//
// llustrates serialization for a simple type
//
class gps_position
{
	//friend std::ostream & operator<<(std::ostream &os, const gps_position &gp);
	friend class boost::serialization::access;
	int degrees;
	int minutes;
	float seconds;
	template<class Archive>
	void serialize(Archive & ar, const unsigned int /* file_version */){
		ar & degrees & minutes & seconds;

		cout << "serialize function " << endl;
	}
public:
	// every serializable class needs a constructor
	gps_position(){
		cout << "constructor 2" << endl;
	};
	gps_position(int _d, int _m, float _s) :
		degrees(_d), minutes(_m), seconds(_s)
	{
		cout << "constructor 1" << endl;
	}
	void print()
	{
		std::cout << degrees << " " << minutes << " " << seconds << std::endl;
	}
};


int main(int argc, char *argv[])
{

	if( argc <= 1 )
	{
		cout << "CREATE AND SAVE" << endl;

		// create and open a character archive for output
		std::ofstream ofs("filename");

		// create class instance
		gps_position g(35, 59, 24.567f);

		g.print();

		// save data to archive
		{
			boost::archive::text_oarchive oa(ofs);
			// write class instance to archive
			oa << g;
			// archive and stream closed when destructors are called
		}
	
	}
	else
	{
		cout << "LOAD" << endl;
		
		// ... some time later restore the class instance to its orginal state
		gps_position newg;
	
			// create and open an archive for input
			std::ifstream ifs("filename");
			boost::archive::text_iarchive ia(ifs);
			// read class state from archive
			ia >> newg;
			// archive and stream closed when destructors are called
			newg.print();
	}
	
	return 0;
}
