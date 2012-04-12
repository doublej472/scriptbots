#ifndef PERFTIMER_H
#define PERFTIMER_H

#include <map>
#include <string>
#include <time.h>
#include <cstdlib>


class PerfTimer
{
public:
	PerfTimer();
	void start(std::string key);
	void end(std::string key);
	void printTimes();

private:
	std::map<std::string, double> totalTimes;
	std::map<std::string, clock_t> intermediateTimes;
};

#endif
