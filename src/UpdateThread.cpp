#include "UpdateThread.h"
#include <time.h>
#include "TeleBoy.h"
#include "http/Cache.h"

#include "kodi/General.h"

#include <chrono>

const time_t maximumUpdateInterval = 600;

std::queue<EpgQueueEntry> UpdateThread::loadEpgQueue;
std::deque<unsigned int> UpdateThread::loadEpgBroadcastQueue;
time_t UpdateThread::nextRecordingsUpdate;
std::mutex UpdateThread::mutex;

UpdateThread::UpdateThread(int threadIdx, TeleBoy& teleboy, Session& session) :
    m_teleboy(teleboy),
    m_session(session),
    m_threadIdx(threadIdx)
{
  time(&UpdateThread::nextRecordingsUpdate);
  UpdateThread::nextRecordingsUpdate += maximumUpdateInterval;

  m_running = true;
  m_thread = std::thread([&] { Process(); });
}

UpdateThread::~UpdateThread()
{
  m_running = false;
  if (m_thread.joinable())
    m_thread.join();
}

void UpdateThread::SetNextRecordingUpdate(time_t nextRecordingsUpdate)
{
  if (nextRecordingsUpdate < UpdateThread::nextRecordingsUpdate)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (nextRecordingsUpdate < UpdateThread::nextRecordingsUpdate)
    {
      UpdateThread::nextRecordingsUpdate = nextRecordingsUpdate;
    }
  }
}

void UpdateThread::LoadEpg(int uniqueChannelId, time_t startTime,
    time_t endTime)
{
  EpgQueueEntry entry;
  entry.uniqueChannelId = uniqueChannelId;
  entry.startTime = startTime;
  entry.endTime = endTime;

  std::lock_guard<std::mutex> lock(mutex);
  loadEpgQueue.push(entry);
}

void UpdateThread::LoadEpgForBroadcast(unsigned int uniqueBroadcastId) {
  std::lock_guard<std::mutex> lock(mutex);
  loadEpgBroadcastQueue.push_back(uniqueBroadcastId);

  // If there are more than 100 entries in the queue, the user was probably just scrolling through the EPG. To avoid making too many requests to the teleboy API,
  // we drop the oldest entries, as they are likely less relevant to the user than the newer ones.
  if (loadEpgBroadcastQueue.size() > 100) {
    loadEpgBroadcastQueue.pop_front();
  }
}

void UpdateThread::Process()
{
  kodi::Log(ADDON_LOG_DEBUG, "Update thread started.");
  while (m_running)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!m_running || !m_session.IsConnected())
    {
      continue;
    }

    if (m_threadIdx == 0) {
      Cache::Cleanup();
    }

    while (!loadEpgQueue.empty())
    {
      std::unique_lock<std::mutex> lock(mutex);
      if (!loadEpgQueue.empty())
      {
        EpgQueueEntry entry = loadEpgQueue.front();
        loadEpgQueue.pop();
        lock.unlock();
        m_teleboy.GetEPGForChannelAsync(entry.uniqueChannelId,
            entry.startTime, entry.endTime);
      }
    }

    while (!loadEpgBroadcastQueue.empty())
    {
      std::unique_lock<std::mutex> lock(mutex);
      if (!loadEpgBroadcastQueue.empty())
      {
        // We process the most-recently added entries first, as they are likely more relevant to the user.
        unsigned int uniqueBroadcastId = loadEpgBroadcastQueue.back();
        loadEpgBroadcastQueue.pop_back();
        lock.unlock();
        m_teleboy.GetEPGForBroadcastAsync(uniqueBroadcastId);
      }
    }

    time_t currentTime;
    time(&currentTime);

    if (currentTime >= UpdateThread::nextRecordingsUpdate)
    {
      std::unique_lock<std::mutex> lock(mutex);
      if (currentTime >= UpdateThread::nextRecordingsUpdate)
      {
        UpdateThread::nextRecordingsUpdate = currentTime
            + maximumUpdateInterval;
        lock.unlock();
        m_teleboy.TriggerTimerUpdate();
        m_teleboy.TriggerRecordingUpdate();
        kodi::Log(ADDON_LOG_DEBUG, "Update thread triggered update.");
      }
    }
  }

  kodi::Log(ADDON_LOG_DEBUG, "Update thread stopped.");
}

