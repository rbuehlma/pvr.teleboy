#ifndef SRC_SESSION_H_
#define SRC_SESSION_H_

#include "http/HttpClient.h"
#include "http/HttpStatusCodeHandler.h"
#include "Utils.h"
#include <thread>

class TeleBoy;

class Session: public HttpStatusCodeHandler
{
public:
  Session(HttpClient* httpClient, TeleBoy* teleboy);
  ~Session();
  ADDON_STATUS Start();
  void Stop();
  void LoginThread();
  void Reset();
  void ErrorStatusCode (int statusCode);
  ADDON_STATUS SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue);
  std::string GetUserId() {
    return m_userId;
  }
  bool GetFavoritesOnly() {
    return m_favoritesOnly;
  }
  bool GetEnableDolby() {
    return m_enableDolby;
  }
  bool GetIsPaidMember() {
    return m_isPlusMember || m_isComfortMember;
  }
  int64_t GetMaxRecallSeconds() {
    return m_maxRecallSeconds;
  }
  bool IsConnected() {
    return m_isConnected;
  }
private:
  bool Login(std::string u, std::string p);
  bool VerifySettings();
  HttpClient* m_httpClient;
  TeleBoy* m_teleBoy;
  std::string m_userId;
  bool m_isPlusMember = false;
  bool m_isComfortMember = false;
  bool m_enableDolby = false;
  bool m_favoritesOnly = false;
  int64_t m_maxRecallSeconds = 60 * 60 * 24 * 7;
  time_t m_nextLoginAttempt = 0;
  bool m_isConnected = false;
  bool m_running = false;
  std::thread m_thread;
};



#endif /* SRC_SESSION_H_ */
