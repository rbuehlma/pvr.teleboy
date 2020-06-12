#pragma once

#include <p8-platform/threads/threads.h>
#include <p8-platform/threads/mutex.h>
#include <queue>

class TeleBoy;

struct EpgQueueEntry
{
  int uniqueChannelId;
  time_t startTime;
  time_t endTime;
};

class UpdateThread: public P8PLATFORM::CThread
{
public:
  UpdateThread(int threadIdx, TeleBoy& teleboy);
  ~UpdateThread() override;
  static void SetNextRecordingUpdate(time_t nextRecordingsUpdate);
  static void LoadEpg(int uniqueChannelId, time_t startTime, time_t endTime);
  void* Process() override;

private:
  TeleBoy& m_teleboy;
  int m_threadIdx;
  static std::queue<EpgQueueEntry> loadEpgQueue;
  static time_t nextRecordingsUpdate;
  static P8PLATFORM::CMutex mutex;
};
