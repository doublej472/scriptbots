#include "World.h"

class Base
{
 public:	
	World * world;

	Base();
	void saveWorld();
	void loadWorld();
};



// TEMPLATE:
/*
  	// Serialization ------------------------------------------
	friend class boost::serialization::access;
	template<class Archive>
	void serialize(Archive & ar, const unsigned int version )
	{
		// Add all class variables here:
		ar & ;

	}
	// ---------------------------------------------------------

*/
