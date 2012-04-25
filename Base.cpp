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
	ofstream ofs("myworld.dat"); // create and open a character archive for output
	boost::archive::text_oarchive oa(ofs);

	// write class instance to archive
	oa << (*world);
	// archive and stream closed when destructors are called

	// Get file size:
	struct stat filestatus;
	stat( "myworld.dat", &filestatus);
	cout << endl << endl << "World Saved: " << filestatus.st_size/1048576 << " MB" << endl;
	
	cout << endl;
	//world->printState();	
}


void Base::loadWorld()
{
	cout << endl << "LOADING FILE" << endl;
		
	ifstream ifs("myworld.dat");
	if( !ifs )
	{
		cout << "Error: File does not exist." << endl << endl;
		exit(0);
	}
	boost::archive::text_iarchive ia(ifs);
		
   	ia >> (*world);

	cout << endl;
	world->printState();

	cout << endl << "Epoch " << world->epoch() << ": ";
}
