#ifndef SRC_HTTP_HTTPCLIENT_H_
#define SRC_HTTP_HTTPCLIENT_H_

#include "Curl.h"
#include "../sql/ParameterDB.h"
#include "HttpStatusCodeHandler.h"

class HttpClient
{
public:
  HttpClient(ParameterDB *parameterDB);
  ~HttpClient();
  std::string HttpGetCached(const std::string& url, time_t cacheDuration, int &statusCode);
  std::string HttpGet(const std::string& url, int &statusCode);
  std::string HttpDelete(const std::string& url, int &statusCode);
  std::string HttpPost(const std::string& url, const std::string& postData, int &statusCode);
  void ClearSession();
  void AddHeader(const std::string& name, const std::string& value);
  void ResetHeaders();
  std::string GetLocation() {
    return m_location;
  }
  void SetApiKey(const std::string& apiKey) {
    m_apiKey = apiKey;
  }
  void SetStatusCodeHandler(HttpStatusCodeHandler* statusCodeHandler) {
    m_statusCodeHandler = statusCodeHandler;
  }

private:
  std::string HttpRequest(const std::string& action, const std::string& url, const std::string& postData, int &statusCode);
  std::string HttpRequestToCurl(Curl &curl, const std::string& action, const std::string& url, const std::string& postData, int &statusCode);
  std::string GenerateUUID();
  std::string m_apiKey;
  std::string m_cinergyS;
  ParameterDB *m_parameterDB;
  std::map<std::string, std::string> m_headers;
  std::string m_location;
  HttpStatusCodeHandler *m_statusCodeHandler = nullptr;
};

#endif /* SRC_HTTP_HTTPCLIENT_H_ */
