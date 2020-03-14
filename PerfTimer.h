#ifndef PERFTIMER_H
#define PERFTIMER_H

#include <map>
#include <string>
//#include <time.h>
#include <cstdlib>
#include <sys/resource.h>
#include <sys/time.h>

class PerfTimer {
public:
  PerfTimer();
  void start(std::string key);
  void end(std::string key);
  void printTimes();
  double getSimpleTime();

private:
  std::map<std::string, double> totalTimes;
  std::map<std::string, double> intermediateTimes;
};

#endif
