#include "Session.h"
#include <kodi/AddonBase.h>
#include <kodi/General.h>
#include "TeleBoy.h"

Session::Session(HttpClient* httpClient, TeleBoy* teleBoy):
  m_httpClient(httpClient),
  m_teleBoy(teleBoy)
{
}

Session::~Session()
{
  m_running = false;
  if (m_thread.joinable())
    m_thread.join();  
}

ADDON_STATUS Session::Start()
{
  if (!VerifySettings()) {
    return ADDON_STATUS_NEED_SETTINGS;
  }
  
  m_running = true;
  m_thread = std::thread([&] { LoginThread(); });
  return ADDON_STATUS_OK;
}

void Session::LoginThread() {
  while (m_running) {
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (m_isConnected) {
      continue;
    }
    
    if (m_nextLoginAttempt > std::time(0)) {
      continue;
    }
    
    m_teleBoy->UpdateConnectionState("Teleboy Connecting", PVR_CONNECTION_STATE_CONNECTING, "");
    
    std::string teleboyUsername = kodi::addon::GetSettingString("username");
    std::string teleboyPassword = kodi::addon::GetSettingString("password");
    m_favoritesOnly = kodi::addon::GetSettingBoolean("favoritesonly");
    m_enableDolby = kodi::addon::GetSettingBoolean("enableDolby");
    
    kodi::Log(ADDON_LOG_DEBUG, "Login Teleboy");
    if (Login(teleboyUsername, teleboyPassword))
    {
      if (!m_teleBoy->SessionInitialized()) {
        m_nextLoginAttempt = std::time(0) + 60;
        continue;
      }
      m_isConnected = true;
      kodi::Log(ADDON_LOG_DEBUG, "Login done");
      m_teleBoy->UpdateConnectionState("Teleboy connection established", PVR_CONNECTION_STATE_CONNECTED, "");
      kodi::QueueNotification(QUEUE_INFO, "", kodi::addon::GetLocalizedString(30105));
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "Login failed");
      m_nextLoginAttempt = std::time(0) + 3600;
      kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30101));
    }
  }
}

bool Session::Login(string u, string p)
{
  m_httpClient->ResetHeaders();
  std::string tbUrl = "https://www.teleboy.ch";
  int statusCode;
  std::string result = m_httpClient->HttpGet(tbUrl + "/live", statusCode);
  
  if (statusCode != 200)
  {
    m_teleBoy->UpdateConnectionState("Not reachable", PVR_CONNECTION_STATE_SERVER_UNREACHABLE, kodi::addon::GetLocalizedString(30104));
    m_nextLoginAttempt = std::time(0) + 60;
    return false;
  }
  
  bool isAuthenticated = result.find("setIsAuthenticated(true") != std::string::npos;
  
  m_httpClient->AddHeader("redirect-limit", "0");

  if (!isAuthenticated) {
    m_nextLoginAttempt = std::time(0) + 60;
    kodi::Log(ADDON_LOG_INFO, "Not yet authenticated. Try to login.");
    m_httpClient->HttpGet(tbUrl + "/login", statusCode);
    std::string location = m_httpClient->GetLocation();
    if (location.find("t.teleboy.ch") != string::npos)
    {
      kodi::Log(ADDON_LOG_INFO, "Using t.teleboy.ch.");
      tbUrl = "https://t.teleboy.ch";
      m_httpClient->HttpGet(tbUrl + "/login", statusCode);
      if (statusCode >= 400) {
        m_teleBoy->UpdateConnectionState("Not reachable", PVR_CONNECTION_STATE_SERVER_UNREACHABLE, kodi::addon::GetLocalizedString(30104));
        m_nextLoginAttempt = std::time(0) + 60;
        return false;
      }
    }
    
    m_httpClient->AddHeader("Referer", tbUrl + "/login");
    result = m_httpClient->HttpPost(tbUrl + "/login_check",
        "login=" + Utils::UrlEncode(u) + "&password=" + Utils::UrlEncode(p)
            + "&keep_login=1", statusCode);
    if (statusCode == 429) {
      m_teleBoy->UpdateConnectionState("Rate limit reached.", PVR_CONNECTION_STATE_ACCESS_DENIED, kodi::addon::GetLocalizedString(30103));
      kodi::Log(ADDON_LOG_ERROR, "Rate limit reached.");
      m_nextLoginAttempt = std::time(0) + 60 * 60 * 2;
      return false;
    }
    if (statusCode >= 400) {
      m_teleBoy->UpdateConnectionState("Login failed", PVR_CONNECTION_STATE_ACCESS_DENIED, kodi::addon::GetLocalizedString(30101));
      kodi::Log(ADDON_LOG_ERROR, "Authentication failed.");
      m_nextLoginAttempt = std::time(0) + 60 * 60 * 2;
      return false;
    }
    
    m_httpClient->ResetHeaders();
    m_httpClient->AddHeader("redirect-limit", "5");
    m_httpClient->AddHeader("Referer", tbUrl + "/login");
    result = m_httpClient->HttpGet(tbUrl, statusCode);
    m_httpClient->ResetHeaders();
    if (result.empty())
    {
      m_teleBoy->UpdateConnectionState("Login failed", PVR_CONNECTION_STATE_ACCESS_DENIED, kodi::addon::GetLocalizedString(30101));
      kodi::Log(ADDON_LOG_ERROR, "Failed to login.");
      m_nextLoginAttempt = std::time(0) + 60 * 60;
      return false;
    }
  } else {
    kodi::Log(ADDON_LOG_INFO, "Already authenticated.");
  }

  size_t pos = result.find("tvapiKey:");
  size_t pos1 = result.find("'", pos) + 1;
  if (pos == std::string::npos || pos1 > pos + 50)
  {
    kodi::Log(ADDON_LOG_ERROR, "No api key found.");
    m_nextLoginAttempt = std::time(0) + 60 * 60;
    return false;
  }
  size_t endPos = result.find("'", pos1);
  if (endPos - pos1 > 65 || endPos <= pos)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Got HTML body: %s", result.c_str());
    kodi::Log(ADDON_LOG_ERROR, "Received api key is invalid.");
    m_nextLoginAttempt = std::time(0) + 60 * 60;
    return false;
  }
  m_httpClient->SetApiKey(result.substr(pos1, endPos - pos1));

  pos = result.find("setId(");
  if (pos == std::string::npos)
  {
    kodi::Log(ADDON_LOG_ERROR, "No user settings found.");
    m_nextLoginAttempt = std::time(0) + 60 * 60;
    return false;
  }
  pos += 6;
  endPos = result.find(")", pos);
  if (endPos - pos > 15 || endPos <= pos)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Got HTML body: %s", result.c_str());
    kodi::Log(ADDON_LOG_ERROR, "Received userId is invalid.");
    m_nextLoginAttempt = std::time(0) + 60 * 60;
    return false;
  }
  m_userId = result.substr(pos, endPos - pos);
  m_isPlusMember = result.find("setIsPlusMember(1", endPos) != std::string::npos;
  m_isComfortMember = result.find("setIsComfortMember(1", endPos)
      != std::string::npos;
  if (!m_isPlusMember) {
    kodi::Log(ADDON_LOG_INFO, "Free accounts are not supported.", m_userId.c_str());
    kodi::QueueNotification(QUEUE_ERROR, "", kodi::addon::GetLocalizedString(30102));
    m_nextLoginAttempt = std::time(0) + 60 * 60;
    return false;
  }
  kodi::Log(ADDON_LOG_DEBUG, "Got userId: %s.", m_userId.c_str());
  
  m_httpClient->AddHeader("Content-Type", "application/json");
  return true;
}

void Session::Reset()
{
  m_isConnected = false;
  m_httpClient->ClearSession();
  m_teleBoy->UpdateConnectionState("Teleboy session expired", PVR_CONNECTION_STATE_CONNECTING, "");
}

ADDON_STATUS Session::SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
  if (!VerifySettings()) {
    return ADDON_STATUS_NEED_SETTINGS;
  }
  Reset();
  return ADDON_STATUS_OK;
}

bool Session::VerifySettings() {
  std::string teleboyUsername = kodi::addon::GetSettingString("username");
  std::string teleboyPassword = kodi::addon::GetSettingString("password");
  if (teleboyUsername.empty() || teleboyPassword.empty()) {
    kodi::Log(ADDON_LOG_INFO, "Username or password not set.");
    kodi::QueueNotification(QUEUE_WARNING, "", kodi::addon::GetLocalizedString(30100));

    return false;
  }
  return true;
}

void Session::ErrorStatusCode (int statusCode) {
}
