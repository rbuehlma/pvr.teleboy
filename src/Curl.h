#include <string>
#include <map>

class Curl
{
public:
  Curl();
  virtual ~Curl();
  virtual std::string Post(std::string url, std::string postData,
      int &statusCode);
  virtual void AddHeader(std::string name, std::string value);
  virtual void ResetHeaders();
  virtual std::string GetSessionId();

private:
  std::string Base64Encode(unsigned char const* in, unsigned int in_len,
      bool urlEncode);
  std::string cookies;
  std::map<std::string, std::string> headers;
};
