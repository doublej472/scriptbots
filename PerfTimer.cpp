#include "PerfTimer.h"
#include <iostream>
#include <stdio.h>
#include <omp.h>
#include <time.h>

using namespace std;

PerfTimer::PerfTimer()
{
	totalTimes = map<string, double>();
	intermediateTimes = map<string, double>();
}

void PerfTimer::start(string key)
{
	//time(&(intermediateTimes[key]));
	intermediateTimes[key] = getSimpleTime();
	//intermediateTimes[key] = time();
}

void PerfTimer::end(string key)
{
	//	time_t endTime;
	//double endTime = getSimpleTime();
	//time(&endTime);
	//totalTimes[key] += (endTime - intermediateTimes[key]);
	//totalTimes[key] += difftime(endTime, intermediateTimes[key]);
	totalTimes[key] += (getSimpleTime() - intermediateTimes[key]);
}

void PerfTimer::printTimes(){
	map<string, double>::iterator it;
	
	cout << endl;
	cout << "-----------------------------------------------------------------" << endl;
	cout << "Timing metrics:   " << endl;

	double total_time = totalTimes["total"];

	for(it=totalTimes.begin(); it != totalTimes.end(); it++)
	{
		//printf("%s : %.2lf\n", it->first.c_str(), it->second);
		printf("%6.2f%% : %s\n", it->second/total_time*100, it->first.c_str());		
		//cout << "      " << it->first << ": " << it->second << endl;
	}
}


double PerfTimer::getSimpleTime(){
	struct timeval t;
	struct timezone tzp;
	gettimeofday(&t, &tzp);
	return t.tv_sec + t.tv_usec*1e-6;
}
