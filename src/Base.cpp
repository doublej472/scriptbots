#include "include/Base.h"

// For getting file size:
#include <sys/stat.h>
#include <sys/types.h> // remove
#include <unistd.h>    //remove

using namespace std;

Base::Base() { world = new World(); }

void Base::saveWorld() {
  printf("Fake world save!\n");
}

void Base::loadWorld() {
  printf("Fake world load!\n");
}
