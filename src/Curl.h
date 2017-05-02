#include <curl/curl.h>
#include <string>

class Curl
{
public:
  Curl();
  virtual ~Curl();
  virtual std::string Post(std::string url, std::string postData, int &statusCode);
  virtual std::string Delete(std::string url);
  virtual void AddHeader(std::string name, std::string value);
  virtual void ResetHeaders();
  virtual std::string GetSessionId();

private:
  virtual std::string Request(std::string action, std::string url, std::string postData, int &statusCode);
  static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
  static size_t HeaderCallback(char *buffer, size_t size, size_t nitems, void *userdata);
  static std::string cookie;
  struct curl_slist *headers;
};
