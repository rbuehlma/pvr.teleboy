#include "Curl.h"
#include "Utils.h"
#include "client.h"

using namespace std;
using namespace ADDON;

static const string setCookie = "Set-Cookie: ";
static const string cinergy_s = "cinergy_s";

string Curl::cookie = "";

Curl::Curl():
  headers(NULL)
{
}

Curl::~Curl() {
  ResetHeaders();
}

size_t Curl::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

size_t Curl::HeaderCallback(char *buffer, size_t size, size_t nitems, void *userdata)
{
  std::string header(buffer, 0, nitems);
  if (strncmp(header.c_str(), (setCookie + cinergy_s).c_str(), (setCookie + cinergy_s).size()) == 0) {
    cookie = header.substr(setCookie.size(), string::npos);
    cookie = cookie.substr(0, cookie.find(";", 0));
  }
  return nitems * size;
}

string Curl::GetSessionId() {
  if (cookie.empty()) {
    return "";
  }
  vector<string> parts = Utils::SplitString(cookie, '=');
  string sessionId = parts[1];
  return sessionId;
}

void Curl::AddHeader(std::string name, std::string value) {
  char buffer[255];
  sprintf(buffer, "%s:%s", name.c_str(), value.c_str());
  headers = curl_slist_append(headers, buffer);
}

void Curl::ResetHeaders() {
  if (headers != NULL) {
    curl_slist_free_all(headers);
    headers = NULL;
  }
}

string Curl::Delete(string url) {
  int statusCode;
  return Request("DELETE", url, "", statusCode);

}

string Curl::Post(string url, string postData, int &statusCode) {
  return Request("", url, postData, statusCode);

}

string Curl::Request(string action, string url, string postData, int &statusCode) {
  CURLcode res;
  CURL *curl;
  string readBuffer;
  char errbuf[CURL_ERROR_SIZE];
  errbuf[0] = 0;

  curl = curl_easy_init();
  if (!curl) {
      XBMC->Log(LOG_ERROR, "curl_easy_init failed.");
      return "";
  }
  if (!action.empty()) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, action.c_str());
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
  if (!postData.empty()) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
  }
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Curl::WriteCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Curl::HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4);
  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

  if (!cookie.empty()) {
    curl_easy_setopt(curl, CURLOPT_COOKIE, cookie.c_str());
  }
  if (headers != NULL) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }
  res = curl_easy_perform(curl);
  if(res != CURLE_OK) {
    XBMC->Log(LOG_ERROR, "Http request failed for url %s with error: %s", url.c_str(), errbuf);
    curl_easy_cleanup(curl);
    return "";
  }
  //curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &statusCode);
  //if (statusCode != 200) {
  //  XBMC->Log(LOG_ERROR, "HTTP failed with status code %i.", statusCode);
  //  curl_easy_cleanup(curl);
  //  return "";
  //}
  curl_easy_cleanup(curl);
  return readBuffer;
}
