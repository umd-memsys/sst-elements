// Copyright 2011 Sandia Corporation. Under the terms                          
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.             
// Government retains certain rights in this software.                         
//                                                                             
// Copyright (c) 2011, Sandia Corporation                                      
// All rights reserved.                                                        
//                                                                             
// This file is part of the SST software package. For license                  
// information, see the LICENSE file in the top level directory of the         
// distribution.                                                               

#include "sst/core/serialization/element.h"
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <time.h>
#include "Statistics.h"
#include "Machine.h"
#include "MachineMesh.h"
#include "Scheduler.h"
#include "Allocator.h"
#include "AllocInfo.h"
#include "misc.h"

using namespace std;
using namespace SST::Scheduler;


struct logInfo {  //information about one type of log that can be created
  string logName;  //name and extension of log
  string header;   //information at top of log
};

const logInfo supportedLogs[] = {
  {"time", "\n# Job \tArrival\tStart\tEnd\tRun\tWait\tResp.\tProcs\n"},
  {"alloc", "\n# Procs Needed\tActual Time\t Pairwise L1 Distance\n"},
  {"visual", ""},   //requires special header
  {"util", "\n# Time\tUtilization\n"},
  {"wait", "\n# Time\tWaiting Jobs\n"}};
const int numSupportedLogs = 5;

enum LOGNAME {  //to use symbolic names on logs; must be updated with supportedLogs
  TIME = 0,
  ALLOC = 1,
  VISUAL = 2,
  UTIL = 3,
  WAIT = 4};
  /*
  UTIL = 1,
  WAIT = 2};
  */

void Statistics::printLogList(ostream& out) {  //print list of possible logs
  for(int i=0; i<numSupportedLogs; i++)
    out << "  " << supportedLogs[i].logName << endl;
}

Statistics::Statistics(Machine* machine, Scheduler* sched, Allocator* alloc,
		       string baseName, char* logList) {
  size_t pos = baseName.rfind("/");
  if(pos == string::npos)
    this -> baseName = baseName;  //didn't find it so entire given string is base
  else
    this -> baseName = baseName.substr(pos+1);

  this -> machine = machine;
  currentTime = 0;
  procsUsed = 0;

  //initialize outputDirectory
  char* dir = getenv("SIMOUTPUT");
  if(dir == NULL)
    outputDirectory = "./";
  else
    outputDirectory = dir;
  
  //initialize fileHeader
  time_t raw;
  time(&raw);
  struct tm* structured = localtime(&raw);
  fileHeader= "# Simulation for trace " + baseName +
    " started " + asctime(structured) + "# [Machine] \n" +
    machine -> getSetupInfo(true) + "\n# [Scheduler] \n" +
    sched -> getSetupInfo(true) + "\n# [Allocator] \n" +
    alloc -> getSetupInfo(true) + "\n";
  
  record = new bool[numSupportedLogs];
  for(int i=0; i<numSupportedLogs; i++)
    record[i] = false;
  char* logName = strtok(logList, ",");
  //  Mesh* mesh = dynamic_cast<Mesh*>(machine);
  while(logName != NULL) {
    bool found = false;
    for(int i=0; !found && i<numSupportedLogs; i++)
      if(logName == supportedLogs[i].logName) {
	found = true;
	
	if(((MachineMesh*)machine == NULL) && ((i == ALLOC) || (i == VISUAL))) {
	  error(string(logName) + " log only implemented for meshes");
	}
	
	initializeLog(logName);
	if(supportedLogs[i].header.length() > 0)
	  appendToLog(supportedLogs[i].header, supportedLogs[i].logName);
	/*
	if(i == VISUAL) {
	  char mesg[100];
	  sprintf(mesg, "MESH %d %d %d\n\n", mesh -> getXDim(),
		  mesh -> getYDim(), mesh -> getZDim());
	  appendToLog(mesg, supportedLogs[VISUAL].logName);
	}
	*/
	record[i] = true;
      }
    if(!found)
      error(string("invalid log name: ") + logName);

    logName = strtok(NULL, ",");
  }

  lastUtil = 0;
  lastUtilTime = -1;
	
  lastWaitTime = -1;
  lastWaitJobs = -1;
  waitingJobs = 0;
  tempWaiting = 0;
}

Statistics::~Statistics() {
  delete[] record;
}

void Statistics::jobArrives(unsigned long time) {   //called when a job has arrived
  tempWaiting++;
  if(record[WAIT])
    writeWaiting(time);
}

void Statistics::jobStarts(AllocInfo* allocInfo, unsigned long time) {
  //called every time a job starts

  if(record[ALLOC])
    writeAlloc(allocInfo);
  /*
  if(record[VISUAL]) {
    char mesg[100];
    sprintf(mesg, "BEGIN %ld ", allocInfo -> job -> getJobNum());
    writeVisual(mesg + allocInfo -> getProcList());
  }
  */

  procsUsed += allocInfo -> job -> getProcsNeeded();
  if(record[UTIL])
    writeUtil(time);
      
  tempWaiting--;
  if(record[WAIT])
    writeWaiting(time);

  currentTime = time;
}

void Statistics::jobFinishes(AllocInfo* allocInfo, unsigned long time) {
  //called every time a job completes

  /*
  if(record[VISUAL]) {
    char mesg[100];
    sprintf(mesg, "END %ld", allocInfo -> job -> getJobNum());
    writeVisual(mesg);
  }
  */

  if(record[TIME])
    writeTime(allocInfo, time);

  procsUsed -= allocInfo -> job -> getProcsNeeded();
  
  if(record[UTIL])
    writeUtil(time);
  
  currentTime = time;
}

void Statistics::writeTime(AllocInfo* allocInfo, unsigned long time) {
  //write time statistics to a file

  unsigned long arrival = allocInfo -> job -> getArrivalTime();
  unsigned long runtime = allocInfo -> job -> getActualTime();
  unsigned long startTime = allocInfo -> job -> getStartTime();
  int procsneeded = allocInfo -> job -> getProcsNeeded();

  char mesg[100];
  sprintf(mesg, "%ld\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%d\n",
	  allocInfo -> job -> getJobNum(),  //Job Num
	  arrival,			    //Arrival time
	  startTime,                        //Start time(currentTime)
	  time,                             //End time
	  runtime,                          //Run time
	  (startTime - arrival),            //Wait time
	  (time - arrival),                 //Response time
	  procsneeded);	                    //Processors needed
  appendToLog(mesg, supportedLogs[TIME].logName);
}


void Statistics::writeAlloc(AllocInfo* allocInfo) {
  //write allocation information to file
  
  MeshAllocInfo* mai = static_cast<MeshAllocInfo*>(allocInfo);
  char mesg[100];
  int num = mai -> job -> getProcsNeeded();
  sprintf(mesg, "%d\t%lu\t%ld\n",
	  num,
	  mai -> job -> getActualTime(),
	  ((MachineMesh*)(machine))-> pairwiseL1Distance(mai -> processors));
  appendToLog(mesg, supportedLogs[ALLOC].logName);
}

void Statistics::writeVisual(string mesg) {
  //write to log for visualization

  appendToLog(mesg + "\n", supportedLogs[VISUAL].logName);
}


void Statistics::writeUtil(unsigned long time) {
  //method to write utilization statistics to file
  //force it to write last entry by setting time = -1

  if(lastUtilTime == (unsigned long)-1) {  //if first observation, just remember it
    lastUtil = procsUsed;
    lastUtilTime = time;
    return;
  }

  if((procsUsed == lastUtil) && (time != (unsigned long)-1))  
    return;  //don't record if utilization unchanged unless forced
  if(lastUtilTime == time) {  //update record of utilization for this time
    lastUtil = procsUsed;
  } else {  //actually record the previous utilization
    char mesg[100];
    sprintf(mesg, "%lu\t%d\n", lastUtilTime, lastUtil);
    appendToLog(mesg, supportedLogs[UTIL].logName);
    lastUtil = procsUsed;
    lastUtilTime = time;
  }
}

void Statistics::writeWaiting(unsigned long time) {
  //possibly add line to log recording number of waiting jobs
  //  (only prints 1 line per time: #waiting jobs after all events at that time)
  //argument is current time or -1 at end of trace

  if(lastWaitTime == (unsigned long)-1) {  //if first observation, just remember it
    lastWaitTime = time;
    return;
  }

  if(lastWaitTime == time) {  //update record of waiting jobs for this time
    waitingJobs = tempWaiting;
    return;
  } else {  //actually record the previous # waiting jobs
    if (lastWaitJobs != waitingJobs) {
      char mesg[100];
      sprintf(mesg, "%lu\t%d\n", lastWaitTime, waitingJobs);
      appendToLog(mesg, supportedLogs[WAIT].logName);
    }
    
    lastWaitJobs = waitingJobs;
    lastWaitTime = time;
    waitingJobs = tempWaiting;
  }
}

void Statistics::done() {  //called after all events have occurred
  if(record[UTIL])
    writeUtil(-1);
  
  if(record[WAIT])
    writeWaiting(-1);
}

void Statistics::initializeLog(string extension) {
  string name = outputDirectory + baseName+ "." + extension;
  ofstream file(name.c_str(), ios::out | ios::trunc);
  if(file.is_open())
    file << fileHeader;
  else
    error("Unable to open file " + name);
  file.close();
}

void Statistics::appendToLog(string mesg, string extension) {
  string name = outputDirectory + baseName+ "." + extension;
  ofstream file(name.c_str(), ios::out | ios::app);
  if(file.is_open())
    file << mesg;
  else
    error("Unable to open file " + name);
  file.close();
}
