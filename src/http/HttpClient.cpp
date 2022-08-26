#include "HttpClient.h"
#include "Cache.h"
#include <random>
#include "../md5.h"
#include <kodi/AddonBase.h>

static const std::string USER_AGENT = std::string("Kodi/")
    + std::string(STR(KODI_VERSION)) + std::string(" pvr.teleboy/")
    + std::string(STR(TELEBOY_VERSION));

static const std::string apiDeviceType = "desktop";
static const std::string apiVersion = "2.0";

HttpClient::HttpClient(ParameterDB *parameterDB):
  m_parameterDB(parameterDB)
{
  kodi::Log(ADDON_LOG_INFO, "Using useragent: %s", USER_AGENT.c_str());

  m_cinergyS = m_parameterDB->Get("cinergy_s");
}

HttpClient::~HttpClient()
{
  
}

void HttpClient::ClearSession() {
  m_cinergyS = "";
  m_parameterDB->Set("cinergy_s", m_cinergyS);
  m_apiKey = "";  
}

std::string HttpClient::HttpGetCached(const std::string& url, time_t cacheDuration, int &statusCode)
{

  std::string content;
  std::string cacheKey = md5(url);
  statusCode = 200;
  if (!Cache::Read(cacheKey, content))
  {
    content = HttpGet(url, statusCode);
    if (!content.empty())
    {
      time_t validUntil;
      time(&validUntil);
      validUntil += cacheDuration;
      Cache::Write(cacheKey, content, validUntil);
    }
  }
  return content;
}

std::string HttpClient::HttpGet(const std::string& url, int &statusCode)
{
  return HttpRequest("GET", url, "", statusCode);
}

std::string HttpClient::HttpDelete(const std::string& url, int &statusCode)
{
  return HttpRequest("DELETE", url, "", statusCode);
}

std::string HttpClient::HttpPost(const std::string& url, const std::string& postData, int &statusCode)
{
  return HttpRequest("POST", url, postData, statusCode);
}

std::string HttpClient::HttpRequest(const std::string& action, const std::string& url, const std::string& postData, int &statusCode)
{
  Curl curl;

  curl.AddOption("acceptencoding", "gzip,deflate");
  
  for (auto const &entry : m_headers)
  {
    curl.AddHeader(entry.first.c_str(), entry.second);
  }
  
  if (!m_cinergyS.empty())
  {
    if (url.find("tv.api.teleboy.ch") != std::string::npos) {
     curl.AddHeader("x-teleboy-session", m_cinergyS);
    } else {
     curl.AddOption("cookie", "cinergy_s=" + m_cinergyS);
    }
  }
  if (!m_apiKey.empty())
  {
    curl.AddHeader("x-teleboy-apikey", m_apiKey);
  }

  curl.AddHeader("x-teleboy-device-type", apiDeviceType);
  curl.AddHeader("x-teleboy-version", apiVersion);
  
  curl.AddHeader("User-Agent", USER_AGENT);

  std::string content = HttpRequestToCurl(curl, action, url, postData, statusCode);
  
  m_location = curl.GetLocation();

  if (statusCode >= 400 || statusCode < 200) {
    kodi::Log(ADDON_LOG_ERROR, "Open URL failed with %i.", statusCode);
    if (m_statusCodeHandler != nullptr) {
      m_statusCodeHandler->ErrorStatusCode(statusCode);
    }
    return content;
  }
  std::string cinergys = curl.GetCookie("cinergy_s");
  if (!cinergys.empty() && cinergys != m_cinergyS && cinergys != "deleted")
  {
    m_cinergyS = cinergys;
    m_parameterDB->Set("cinergy_s", m_cinergyS);
  }

  return content;
}

std::string HttpClient::HttpRequestToCurl(Curl &curl, const std::string& action,
    const std::string& url, const std::string& postData, int &statusCode)
{
  kodi::Log(ADDON_LOG_DEBUG, "Http-Request: %s %s.", action.c_str(), url.c_str());
  std::string content;
  if (action == "POST")
  {
    content = curl.Post(url, postData, statusCode);
  }
  else if (action == "DELETE")
  {
    content = curl.Delete(url, statusCode);
  }
  else
  {
    content = curl.Get(url, statusCode);
  }
  return content;

}

void HttpClient::AddHeader(const std::string& name, const std::string& value)
{
  m_headers[name] = value;
}

void HttpClient::ResetHeaders()
{
  m_headers.clear();
}
