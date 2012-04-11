#include "World.h"

class Base
{
 public:	
	World * world;

	Base();
	void saveWorld();
	void loadWorld();
	void runWorld(int ticks);
};
