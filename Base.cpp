#include "Base.h"

// For getting file size:
#include <sys/types.h> // remove
#include <sys/stat.h>
#include <unistd.h> //remove

using namespace std;

Base::Base()
{
	world = new World();
}

void Base::saveWorld()
{
	// save data to archive -----------------------------
	cout << endl << "SAVING" << endl;
	
	ofstream ofs("myworld.dat"); // create and open a character archive for output
	boost::archive::text_oarchive oa(ofs);

	// write class instance to archive
	oa << (*world);
	// archive and stream closed when destructors are called

	// Get file size:
	struct stat filestatus;
	stat( "myworld.dat", &filestatus);
	cout << "World file is " << filestatus.st_size/1048576 << " MB" << endl;
	
	cout << endl;
	world->printState();	
}


void Base::loadWorld()
{
	cout << endl << "LOADING" << endl;
		
	ifstream ifs("myworld.dat");
	boost::archive::text_iarchive ia(ifs);
		
	//cout << "\tBetween read and unpack" << endl;
   	ia >> (*world);

	cout << endl;
	world->printState();

	cout << endl << "Epoch " << world->epoch() << ": ";
}


void Base::runWorld(int ticks)
{
	for(int i = 0; i < ticks; ++i)		
		world->update();


}
