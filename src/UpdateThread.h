#pragma once

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include "Session.h"

class TeleBoy;

struct EpgQueueEntry
{
  int uniqueChannelId;
  time_t startTime;
  time_t endTime;
};

class UpdateThread
{
public:
  UpdateThread(int threadIdx, TeleBoy& teleboy, Session& session);
  ~UpdateThread();
  static void SetNextRecordingUpdate(time_t nextRecordingsUpdate);
  static void LoadEpg(int uniqueChannelId, time_t startTime, time_t endTime);
  void Process();

private:
  TeleBoy& m_teleboy;
  Session& m_session;
  int m_threadIdx;
  static std::queue<EpgQueueEntry> loadEpgQueue;
  static time_t nextRecordingsUpdate;
  std::atomic<bool> m_running = {false};
  std::thread m_thread;
  static std::mutex mutex;
};
