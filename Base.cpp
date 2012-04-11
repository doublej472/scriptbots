#include "Base.h"

using namespace std;

Base::Base()
{
	world = new World();
	//world->init();
}

void Base::saveWorld()
{
	// save data to archive -----------------------------
	cout << endl << endl << "Saving file now: " << endl;
	
	ofstream ofs("myworld.dat"); // create and open a character archive for output
	
	boost::archive::text_oarchive oa(ofs);

	// write class instance to archive
	//	oa << (*world);
	oa << (*world);
	// archive and stream closed when destructors are called
	
	std::cout << "Saving with modcounter " << world->modcounter << std::endl;
}


void Base::loadWorld()
{
	cout << "LOAD" << endl;
		
	ifstream ifs("myworld.dat");
	boost::archive::text_iarchive ia(ifs);
		
	cout << "\tBetween read and unpack" << endl;
   	ia >> (*world);

	printf("\tModcounter = %d \n", world->modcounter);	
}


void Base::runWorld(int ticks)
{
	for(int i = 0; i < ticks; ++i)		
		world->update();


}
