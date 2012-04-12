#include "PerfTimer.h"
#include <iostream>

using namespace std;

PerfTimer::PerfTimer()
{
	totalTimes = map<string, double>();
	intermediateTimes = map<string, clock_t>();
}

void PerfTimer::start(string key)
{
	intermediateTimes[key] = clock();
}

void PerfTimer::end(string key)
{
	clock_t endTime = clock();
	totalTimes[key] += ((endTime - intermediateTimes[key])/(double)CLOCKS_PER_SEC);
}

void PerfTimer::printTimes(){
	map<string, double>::iterator it;
	
	cout << endl;
	cout << "-----------------------------------------------------------------" << endl;
	cout << "Timing metrics:   " << endl;
	
	for(it=totalTimes.begin(); it != totalTimes.end(); it++)
	{
		cout << "      " << it->first << ": " << it->second << endl;
	}
}
