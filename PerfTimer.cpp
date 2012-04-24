#include "PerfTimer.h"
#include <iostream>
#include <stdio.h>
#include <omp.h>
#include <time.h>

using namespace std;

PerfTimer::PerfTimer()
{
	totalTimes = map<string, double>();
	intermediateTimes = map<string, time_t>();
}

void PerfTimer::start(string key)
{
	time(&(intermediateTimes[key]));
	//intermediateTimes[key] = time();
}

void PerfTimer::end(string key)
{
	time_t endTime;
	time(&endTime);
	//totalTimes[key] += (endTime - intermediateTimes[key]);
	totalTimes[key] += difftime(endTime, intermediateTimes[key]);
}

void PerfTimer::printTimes(){
	map<string, double>::iterator it;
	
	cout << endl;
	cout << "-----------------------------------------------------------------" << endl;
	cout << "Timing metrics:   " << endl;
	
	for(it=totalTimes.begin(); it != totalTimes.end(); it++)
	{
		printf("%s : %.2lf\n", it->first.c_str(), it->second);
		//cout << "      " << it->first << ": " << it->second << endl;
	}
}
