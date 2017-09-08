#include "UpdateThread.h"
#include <time.h>
#include "client.h"
#include "TeleBoy.h"

using namespace ADDON;

const time_t maximumUpdateInterval = 600;

UpdateThread::UpdateThread(void *teleboy) :
    CThread()
{
  this->teleboy = teleboy;
  time(&nextRecordingsUpdate);
  nextRecordingsUpdate += maximumUpdateInterval;
  CreateThread(false);
}

UpdateThread::~UpdateThread()
{

}

void UpdateThread::SetNextRecordingUpdate(time_t nextRecordingsUpdate)
{
  if (nextRecordingsUpdate < this->nextRecordingsUpdate)
  {
    this->nextRecordingsUpdate = nextRecordingsUpdate;
  }
}

void UpdateThread::LoadEpg(int uniqueChannelId, time_t startTime,
    time_t endTime)
{
  EpgQueueEntry entry;
  entry.uniqueChannelId = uniqueChannelId;
  entry.startTime = startTime;
  entry.endTime = endTime;
  loadEpgQueue.push(entry);
}


void* UpdateThread::Process()
{
  XBMC->Log(LOG_DEBUG, "Update thread started.");
  while (!IsStopped())
  {    
    while (!loadEpgQueue.empty() && !IsStopped())
    {
      EpgQueueEntry entry = loadEpgQueue.front();
      loadEpgQueue.pop();
      ((TeleBoy*) teleboy)->GetEPGForChannelAsync(entry.uniqueChannelId,
          entry.startTime, entry.endTime);
    }
    time_t currentTime;
    time(&currentTime);
    if (currentTime < nextRecordingsUpdate)
    {
      Sleep(100);
      continue;
    }
    nextRecordingsUpdate = currentTime + maximumUpdateInterval;
    PVR->TriggerTimerUpdate();
    PVR->TriggerRecordingUpdate();
    XBMC->Log(LOG_DEBUG, "Update thread triggered update.");
    Sleep(100);
  }
  XBMC->Log(LOG_DEBUG, "Update thread stopped.");
  return 0;
}

