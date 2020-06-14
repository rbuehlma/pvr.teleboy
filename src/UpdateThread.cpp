#include "UpdateThread.h"
#include <time.h>
#include "TeleBoy.h"
#include "Cache.h"

#include "kodi/General.h"

const time_t maximumUpdateInterval = 600;

std::queue<EpgQueueEntry> UpdateThread::loadEpgQueue;
time_t UpdateThread::nextRecordingsUpdate;
P8PLATFORM::CMutex UpdateThread::mutex;

UpdateThread::UpdateThread(int threadIdx, TeleBoy& teleboy) :
    CThread(),
    m_teleboy(teleboy),
    m_threadIdx(threadIdx)
{
  time(&UpdateThread::nextRecordingsUpdate);
  UpdateThread::nextRecordingsUpdate += maximumUpdateInterval;
  CreateThread(false);
}

UpdateThread::~UpdateThread()
{

}

void UpdateThread::SetNextRecordingUpdate(time_t nextRecordingsUpdate)
{
  if (nextRecordingsUpdate < UpdateThread::nextRecordingsUpdate)
  {
    if (!mutex.Lock())
    {
      kodi::Log(ADDON_LOG_ERROR,
          "UpdateThread::SetNextRecordingUpdate : Could not lock mutex.");
      return;
    }
    if (nextRecordingsUpdate < UpdateThread::nextRecordingsUpdate)
    {
      UpdateThread::nextRecordingsUpdate = nextRecordingsUpdate;
    }
    mutex.Unlock();
  }
}

void UpdateThread::LoadEpg(int uniqueChannelId, time_t startTime,
    time_t endTime)
{
  EpgQueueEntry entry;
  entry.uniqueChannelId = uniqueChannelId;
  entry.startTime = startTime;
  entry.endTime = endTime;
  if (!mutex.Lock())
  {
    kodi::Log(ADDON_LOG_ERROR, "UpdateThread::LoadEpg : Could not lock mutex.");
    return;
  }
  loadEpgQueue.push(entry);
  mutex.Unlock();
}

void* UpdateThread::Process()
{
  kodi::Log(ADDON_LOG_DEBUG, "Update thread started.");
  while (!IsStopped())
  {
    Sleep(100);
    if (IsStopped())
    {
      continue;
    }

    if (m_threadIdx == 0) {
      Cache::Cleanup();
    }

    while (!loadEpgQueue.empty())
    {
      if (!mutex.Lock())
      {
        kodi::Log(ADDON_LOG_ERROR,
            "UpdateThread::Process : Could not lock mutex for epg queue");
        break;
      }
      if (!loadEpgQueue.empty())
      {
        EpgQueueEntry entry = loadEpgQueue.front();
        loadEpgQueue.pop();
        mutex.Unlock();
        m_teleboy.GetEPGForChannelAsync(entry.uniqueChannelId,
            entry.startTime, entry.endTime);
      }
      else
      {
        mutex.Unlock();
      }
    }

    time_t currentTime;
    time(&currentTime);

    if (currentTime >= UpdateThread::nextRecordingsUpdate)
    {
      if (!mutex.Lock())
      {
        kodi::Log(ADDON_LOG_ERROR,
            "UpdateThread::Process : Could not lock mutex for recordings update");
        continue;
      }
      if (currentTime >= UpdateThread::nextRecordingsUpdate)
      {
        UpdateThread::nextRecordingsUpdate = currentTime
            + maximumUpdateInterval;
        mutex.Unlock();
        m_teleboy.TriggerTimerUpdate();
        m_teleboy.TriggerRecordingUpdate();
        kodi::Log(ADDON_LOG_DEBUG, "Update thread triggered update.");
      }
      else
      {
        mutex.Unlock();
      }
    }
  }

  kodi::Log(ADDON_LOG_DEBUG, "Update thread stopped.");
  return nullptr;
}

