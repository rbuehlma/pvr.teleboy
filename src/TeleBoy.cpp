#include <algorithm>
#include <iostream>
#include <string>
#include "TeleBoy.h"
#include <sstream>
#include "p8-platform/sockets/tcp.h"
#include <map>
#include <time.h>
#include <random>
#include "Utils.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#ifdef TARGET_ANDROID
#include "to_string.h"
#endif

using namespace ADDON;
using namespace std;
using namespace rapidjson;

static const string apiUrl = "https://tv.api.teleboy.ch";
static const string apiDeviceType = "desktop";
static const string apiVersion = "2.0";
const char data_file[] = "special://profile/addon_data/pvr.teleboy/data.json";
P8PLATFORM::CMutex TeleBoy::sendEpgToKodiMutex;

string TeleBoy::HttpGet(Curl &curl, string url)
{
  return HttpRequest(curl, "GET", url, "");
}

string TeleBoy::HttpDelete(Curl &curl, string url)
{
  return HttpRequest(curl, "DELETE", url, "");
}

string TeleBoy::HttpPost(Curl &curl, string url, string postData)
{
  return HttpRequest(curl, "POST", url, postData);
}

string TeleBoy::HttpRequest(Curl &curl, string action, string url,
    string postData)
{
  int statusCode;
  XBMC->Log(LOG_DEBUG, "Http-Request: %s %s.", action.c_str(), url.c_str());
  string content;
  if (action.compare("POST") == 0)
  {
    content = curl.Post(url, postData, statusCode);
  }
  else if (action.compare("DELETE") == 0)
  {
    content = curl.Delete(url, statusCode);
  }
  else
  {
    content = curl.Get(url, statusCode);
  }
  string cinergys = curl.GetCookie("cinergy_s");
  if (!cinergys.empty() && cinergys != cinergySCookies)
  {
    cinergySCookies = cinergys;
    WriteDataJson();
  }
  return content;
}

void TeleBoy::ApiSetHeader(Curl &curl)
{
  curl.AddHeader("x-teleboy-apikey", apiKey);
  curl.AddHeader("x-teleboy-device-type", apiDeviceType);
  curl.AddHeader("x-teleboy-session", cinergySCookies);
  curl.AddHeader("x-teleboy-version", apiVersion);
}

bool TeleBoy::ApiGetResult(string content, Document &doc)
{
  doc.Parse(content.c_str());
  if (!doc.GetParseError())
  {
    if (doc["success"].GetBool())
    {
      return true;
    }
  }
  return false;
}

bool TeleBoy::ApiGet(string url, Document &doc)
{
  Curl curl;
  ApiSetHeader(curl);
  string content = HttpGet(curl, apiUrl + url);
  curl.ResetHeaders();
  return ApiGetResult(content, doc);
}

bool TeleBoy::ApiPost(string url, string postData, Document &doc)
{
  Curl curl;
  ApiSetHeader(curl);
  if (!postData.empty())
  {
    curl.AddHeader("Content-Type", "application/json");
  }
  string content = HttpPost(curl, apiUrl + url, postData);
  curl.ResetHeaders();
  return ApiGetResult(content, doc);
}

bool TeleBoy::ApiDelete(string url, Document &doc)
{
  Curl curl;
  ApiSetHeader(curl);
  string content = HttpDelete(curl, apiUrl + url);
  curl.ResetHeaders();
  return ApiGetResult(content, doc);
}

TeleBoy::TeleBoy(bool favoritesOnly, bool enableDolby) :
    username(""), password(""), maxRecallSeconds(60 * 60 * 24 * 7), cinergySCookies(
        ""), isPlusMember(false), isComfortMember(false)
{
  ReadDataJson();
  for (int i = 0; i < 5; ++i)
  {
    updateThreads.emplace_back(new UpdateThread(this));
  }
  this->favoritesOnly = favoritesOnly;
  this->enableDolby = enableDolby;
}

TeleBoy::~TeleBoy()
{
  for (auto const &updateThread : updateThreads)
  {
    updateThread->StopThread(200);
    delete updateThread;
  }
}

bool TeleBoy::Login(string u, string p)
{
  string tbUrl = "https://www.teleboy.ch";
  Curl curl;
  if (!cinergySCookies.empty())
  {
    curl.AddOption("cookie", "cinergy_s=" + cinergySCookies);
  }
  string result = HttpGet(curl, tbUrl + "/live");
  bool isAuthenticated = result.find("setIsAuthenticated(true") != std::string::npos;
  curl.AddHeader("redirect-limit", "0");
  
  if (!isAuthenticated) {
    XBMC->Log(LOG_INFO, "Not yet authenticated. Try to login.");
    HttpGet(curl, tbUrl + "/login");
    string location = curl.GetLocation();
    if (location.find("t.teleboy.ch") != string::npos)
    {
      XBMC->Log(LOG_NOTICE, "Using t.teleboy.ch.");
      tbUrl = "https://t.teleboy.ch";
      HttpGet(curl, tbUrl + "/login");
    }
    curl.AddHeader("Referer", tbUrl + "/login");
    if (!cinergySCookies.empty())
    {
      curl.AddOption("cookie", "cinergy_s=" + cinergySCookies);
    }
    result = HttpPost(curl, tbUrl + "/login_check",
        "login=" + Utils::UrlEncode(u) + "&password=" + Utils::UrlEncode(p)
            + "&keep_login=1");
    curl.ResetHeaders();
    curl.AddHeader("redirect-limit", "5");
    curl.AddHeader("Referer", tbUrl + "/login");
    if (!cinergySCookies.empty())
    {
      curl.AddOption("cookie", "welcomead=1; cinergy_s=" + cinergySCookies);
    }
    result = HttpGet(curl, tbUrl);
    curl.ResetHeaders();
    if (result.empty())
    {
      XBMC->Log(LOG_ERROR, "Failed to login.");
      return false;
    }
  } else {
    XBMC->Log(LOG_INFO, "Already authenticated.");
  }
  
  size_t pos = result.find("tvapiKey:");
  size_t pos1 = result.find("'", pos) + 1;
  if (pos == std::string::npos || pos1 > pos + 50)
  {
    XBMC->Log(LOG_ERROR, "No api key found.");
    return false;
  }
  size_t endPos = result.find("'", pos1);
  if (endPos - pos1 > 65 || endPos <= pos)
  {
    XBMC->Log(LOG_DEBUG, "Got HTML body: %s", result.c_str());
    XBMC->Log(LOG_ERROR, "Received api key is invalid.");
    return false;
  }
  apiKey = result.substr(pos1, endPos - pos1);

  pos = result.find("setId(");
  if (pos == std::string::npos)
  {
    XBMC->Log(LOG_ERROR, "No user settings found.");
    return false;
  }
  pos += 6;
  endPos = result.find(")", pos);
  if (endPos - pos > 15 || endPos <= pos)
  {
    XBMC->Log(LOG_DEBUG, "Got HTML body: %s", result.c_str());
    XBMC->Log(LOG_ERROR, "Received userId is invalid.");
    return false;
  }
  userId = result.substr(pos, endPos - pos);

  isPlusMember = result.find("setIsPlusMember(1", endPos) != std::string::npos;
  isComfortMember = result.find("setIsComfortMember(1", endPos)
      != std::string::npos;
  XBMC->Log(LOG_DEBUG, "Got userId: %s.", userId.c_str());
  return true;
}

void TeleBoy::GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsRecordings = true;
  pCapabilities->bSupportsTimers = true;
}

void TeleBoy::LoadGenres()
{
  Document json;
  if (!ApiGet("/epg/genres", json))
  {
    XBMC->Log(LOG_ERROR, "Error loading genres.");
    return;
  }
  Value& genres = json["data"]["items"];
  for (Value::ConstValueIterator itr1 = genres.Begin();
      itr1 != genres.End(); ++itr1)
  {
    const Value &genre = (*itr1);
    TeleboyGenre teleboyGenre;
    int id = genre["id"].GetInt();
    teleboyGenre.name = GetStringOrEmpty(genre, "name");
    teleboyGenre.nameEn = GetStringOrEmpty(genre, "name_en");
    genresById[id] = teleboyGenre;
    
    if (genre.HasMember("sub_genres")) {
      const Value& subGenres = genre["sub_genres"];

      for (Value::ConstValueIterator itr1 = subGenres.Begin();
          itr1 != subGenres.End(); ++itr1)
      {
        const Value &subGenre = (*itr1);
        TeleboyGenre teleboySubGenre;
        int subId = subGenre["id"].GetInt();
        teleboySubGenre.name = GetStringOrEmpty(subGenre, "name");
        teleboySubGenre.nameEn = GetStringOrEmpty(subGenre, "name_en");
        genresById[subId] = teleboySubGenre;
      }
    }
  }
}

bool TeleBoy::LoadChannels()
{
  Document json;
  if (!ApiGet("/epg/stations?expand=logos&language=de", json))
  {
    XBMC->Log(LOG_ERROR, "Error loading channels.");
    return false;
  }
  Value& channels = json["data"]["items"];
  for (Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    const Value &c = (*itr1);
    if (!c["has_stream"].GetBool())
    {
      continue;
    }
    TeleBoyChannel channel;
    channel.id = c["id"].GetInt();
    channel.name = GetStringOrEmpty(c, "name");
    channel.logoPath = "https://www.teleboy.ch/assets/stations/"
        + to_string(channel.id) + "/icon320_dark.png";
    channelsById[channel.id] = channel;
  }

  if (!ApiGet("/users/" + userId + "/stations", json))
  {
    XBMC->Log(LOG_ERROR, "Error loading sorted channels.");
    return false;
  }
  channels = json["data"]["items"];
  for (Value::ConstValueIterator itr1 = channels.Begin();
      itr1 != channels.End(); ++itr1)
  {
    int cid = (*itr1).GetInt();
    if (channelsById.find(cid) != channelsById.end())
    {
      sortedChannels.push_back(cid);
    }
  }
  return true;
}

int TeleBoy::GetChannelsAmount(void)
{
  if (favoritesOnly)
  {
    return sortedChannels.size();
  }
  return channelsById.size();
}

PVR_ERROR TeleBoy::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  int channelNum = 0;
  for (int const &cid : sortedChannels)
  {
    channelNum++;
    TransferChannel(handle, channelsById[cid], channelNum);
  }
  if (!favoritesOnly)
  {
    for (auto const &item : channelsById)
    {
      if (std::find(sortedChannels.begin(), sortedChannels.end(), item.first)
          != sortedChannels.end())
      {
        continue;
      }
      channelNum++;
      TransferChannel(handle, item.second, channelNum);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

void TeleBoy::TransferChannel(ADDON_HANDLE handle, TeleBoyChannel channel,
    int channelNum)
{
  PVR_CHANNEL kodiChannel;
  memset(&kodiChannel, 0, sizeof(PVR_CHANNEL));

  kodiChannel.iUniqueId = channel.id;
  kodiChannel.bIsRadio = false;
  kodiChannel.iChannelNumber = channelNum;
  PVR_STRCPY(kodiChannel.strChannelName, channel.name.c_str());
  PVR_STRCPY(kodiChannel.strIconPath, channel.logoPath.c_str());
  PVR->TransferChannelEntry(handle, &kodiChannel);
}

string TeleBoy::GetChannelStreamUrl(int uniqueId)
{
  Document json;
  if (!ApiGet(
      "/users/" + userId + "/stream/live/" + to_string(uniqueId)
          + "?expand=primary_image,flags&https=1" + GetStreamParameters(), json))
  {
    XBMC->Log(LOG_ERROR, "Error getting live stream url for channel %i.",
        uniqueId);
    return "";
  }
  string url = GetStringOrEmpty(json["data"]["stream"], "url");
  XBMC->Log(LOG_INFO, "Play URL: %s.", url.c_str());
  url = FollowRedirect(url);
  return url;
}

string TeleBoy::FollowRedirect(string url)
{
  Curl curl;
  curl.AddHeader("redirect-limit", "0");
  string currUrl = url;
  for (int i = 0; i < 5; i++)
  {
    int statusCode;
    curl.Get(currUrl, statusCode);
    string nextUrl = curl.GetLocation();
    if (nextUrl.empty())
    {
      XBMC->Log(LOG_DEBUG, "Final url : %s.", currUrl.c_str());
      return currUrl;
    }
    XBMC->Log(LOG_DEBUG, "Redirected to : %s.", nextUrl.c_str());
    currUrl = nextUrl;
  }
  return currUrl;
}

void TeleBoy::GetEPGForChannel(const PVR_CHANNEL &channel, time_t iStart,
    time_t iEnd)
{
  UpdateThread::LoadEpg(channel.iUniqueId, iStart, iEnd);
}

void TeleBoy::GetEPGForChannelAsync(int uniqueChannelId, time_t iStart,
    time_t iEnd)
{
  int totals = -1;
  int sum = 0;
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + userId + "/broadcasts?begin=" + formatDateTime(iStart)
            + "&end=" + formatDateTime(iEnd) + "&expand=logos&limit=500&skip="
            + to_string(sum) + "&sort=station&station="
            + to_string(uniqueChannelId), json))
    {
      XBMC->Log(LOG_ERROR, "Error getting epg for channel %i.",
          uniqueChannelId);
      return;
    }
    totals = json["data"]["total"].GetInt();
    const Value& items = json["data"]["items"];
    
    if (!sendEpgToKodiMutex.Lock()) {
      XBMC->Log(LOG_ERROR, "Failed to lock sendEpgToKodiMutex.");
      return;
    }
    
    for (Value::ConstValueIterator itr1 = items.Begin(); itr1 != items.End();
        ++itr1)
    {
      const Value& item = (*itr1);
      sum++;
      EPG_TAG tag;
      memset(&tag, 0, sizeof(EPG_TAG));
      tag.iUniqueBroadcastId = item["id"].GetInt();
      tag.strTitle = strdup(GetStringOrEmpty(item, "title").c_str());
      tag.iUniqueChannelId = uniqueChannelId;
      tag.startTime = Utils::StringToTime(GetStringOrEmpty(item, "begin"));
      tag.endTime = Utils::StringToTime(GetStringOrEmpty(item, "end"));
      tag.strPlotOutline = strdup(GetStringOrEmpty(item, "headline").c_str());
      tag.strPlot = strdup(GetStringOrEmpty(item, "short_description").c_str());
      tag.strOriginalTitle = strdup(GetStringOrEmpty(item, "original_title").c_str());
      tag.strCast = nullptr; /* not supported */
      tag.strDirector = nullptr; /*SA not supported */
      tag.strWriter = nullptr; /* not supported */
      tag.iYear = item.HasMember("year") ? item["year"].GetInt() : 0;
      tag.strIMDBNumber = nullptr; /* not supported */
      tag.strIconPath = nullptr; /* not supported */
      tag.iParentalRating = 0; /* not supported */
      tag.iStarRating = 0; /* not supported */
      tag.bNotify = false; /* not supported */
      tag.iSeriesNumber =
          item.HasMember("serie_season") ? item["serie_season"].GetInt() : 0;
      tag.iEpisodeNumber =
          item.HasMember("serie_episode") ? item["serie_episode"].GetInt() : 0;
      tag.iEpisodePartNumber = 0; /* not supported */
      tag.strEpisodeName = strdup(GetStringOrEmpty(item, "subtitle").c_str());
      if (item.HasMember("genre_id")) {
        int genreId = item["genre_id"].GetInt();
        TeleboyGenre genre = genresById[genreId];
        int kodiGenre = m_categories.Category(genre.nameEn);
        if (kodiGenre == 0) {
          tag.iGenreType = EPG_GENRE_USE_STRING;
          tag.iGenreSubType = 0;
          tag.strGenreDescription = genre.name.c_str();
        } else {
          tag.iGenreSubType = kodiGenre & 0x0F;
          tag.iGenreType = kodiGenre & 0xF0;
        }
      }
      tag.iFlags = EPG_TAG_FLAG_UNDEFINED;

      PVR->EpgEventStateChange(&tag, EPG_EVENT_CREATED);
    }
    sendEpgToKodiMutex.Unlock();
    XBMC->Log(LOG_DEBUG, "Loaded %i of %i epg entries for channel %i.", sum,
        totals, uniqueChannelId);
  }
  return;
}

string TeleBoy::formatDateTime(time_t dateTime)
{
  char buff[20];
  struct tm tm;
  gmtime_r(&dateTime, &tm);
  strftime(buff, 20, "%Y-%m-%d+%H:%M:%S", &tm);
  return buff;
}

bool TeleBoy::Record(int programId)
{
  string postData = "{\"broadcast\": " + to_string(programId)
      + ", \"alternative\": false}";
  Document json;
  if (!ApiPost("/users/" + userId + "/recordings", postData, json))
  {
    XBMC->Log(LOG_ERROR, "Error recording program %i.", programId);
    return false;
  }
  return true;
}

bool TeleBoy::DeleteRecording(string recordingId)
{
  Document doc;
  if (!ApiDelete("/users/" + userId + "/recordings/" + recordingId, doc))
  {
    XBMC->Log(LOG_ERROR, "Error deleting recording %s.", recordingId.c_str());
    return false;
  }
  return true;
}

void TeleBoy::GetRecordings(ADDON_HANDLE handle, string type)
{
  int totals = -1;
  int sum = 0;
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + userId + "/recordings/" + type
            + "?desc=1&expand=flags,logos&limit=100&skip=" + to_string(sum) + "&sort=date", json))
    {
      XBMC->Log(LOG_ERROR, "Error getting recordings of type %s.",
          type.c_str());
      return;
    }
    totals = json["data"]["total"].GetInt();
    const Value& items = json["data"]["items"];
    for (Value::ConstValueIterator itr1 = items.Begin(); itr1 != items.End();
        ++itr1)
    {
      const Value& item = (*itr1);
      sum++;

      if (type.find("planned") == 0)
      {
        PVR_TIMER tag;
        memset(&tag, 0, sizeof(PVR_TIMER));

        tag.iClientIndex = item["id"].GetInt();
        PVR_STRCPY(tag.strTitle, GetStringOrEmpty(item, "title").c_str());
        PVR_STRCPY(tag.strSummary, GetStringOrEmpty(item, "subtitle").c_str());
        tag.startTime = Utils::StringToTime(GetStringOrEmpty(item, "begin"));
        tag.endTime = Utils::StringToTime(GetStringOrEmpty(item, "end"));
        tag.state = PVR_TIMER_STATE_SCHEDULED;
        tag.iTimerType = 1;
        tag.iEpgUid = item["id"].GetInt();
        tag.iClientChannelUid = item["station_id"].GetInt();
        PVR->TransferTimerEntry(handle, &tag);
        UpdateThread::SetNextRecordingUpdate(tag.endTime + 60 * 21);

      }
      else
      {
        PVR_RECORDING tag;
        memset(&tag, 0, sizeof(PVR_RECORDING));
        tag.bIsDeleted = false;
        PVR_STRCPY(tag.strRecordingId, to_string(item["id"].GetInt()).c_str());
        PVR_STRCPY(tag.strTitle, GetStringOrEmpty(item, "title").c_str());
        PVR_STRCPY(tag.strEpisodeName, GetStringOrEmpty(item, "subtitle").c_str());
        PVR_STRCPY(tag.strPlot, GetStringOrEmpty(item, "description").c_str());
        PVR_STRCPY(tag.strPlotOutline, GetStringOrEmpty(item, "short_description").c_str());
        tag.iChannelUid = item["station_id"].GetInt();
        PVR_STRCPY(tag.strIconPath, channelsById[tag.iChannelUid].logoPath.c_str());
        PVR_STRCPY(tag.strChannelName, channelsById[tag.iChannelUid].name.c_str());
        tag.recordingTime = Utils::StringToTime(GetStringOrEmpty(item, "begin"));
        time_t endTime = Utils::StringToTime(GetStringOrEmpty(item, "end"));
        tag.iDuration = endTime - tag.recordingTime;
        tag.iEpgEventId = item["id"].GetInt();

        PVR->TransferRecordingEntry(handle, &tag);
      }
    }
  }
}

string TeleBoy::GetRecordingStreamUrl(string recordingId)
{
  Document json;
  if (!ApiGet("/users/" + userId + "/stream/" + recordingId + "?" + GetStreamParameters(), json))
  {
    XBMC->Log(LOG_ERROR, "Could not get URL for recording: %s.",
        recordingId.c_str());
    return "";
  }
  string url = GetStringOrEmpty(json["data"]["stream"], "url");
  url = FollowRedirect(url);
  return url;
}

bool TeleBoy::IsPlayable(const EPG_TAG *tag)
{
  if (!isComfortMember && !isPlusMember)
  {
    return false;
  }
  time_t current_time;
  time(&current_time);
  return ((current_time - tag->endTime) < maxRecallSeconds)
      && (tag->startTime < current_time);
}

bool TeleBoy::IsRecordable(const EPG_TAG *tag)
{
  time_t current_time;
  time(&current_time);
  return ((current_time - tag->endTime) < maxRecallSeconds);
}

string TeleBoy::GetEpgTagUrl(const EPG_TAG *tag)
{
  Document json;
  if (!ApiGet(
      "/users/" + userId + "/stream/"+ to_string(tag->iUniqueBroadcastId) + "?" + GetStreamParameters()
          , json))
  {
    XBMC->Log(LOG_ERROR, "Could not get URL for epg tag.");
    return "";
  }
  string url = GetStringOrEmpty(json["data"]["stream"], "url");
  url = FollowRedirect(url);
  return url;
}

string TeleBoy::GetStringOrEmpty(const Value& jsonValue, const char* fieldName)
{
  if (!jsonValue.HasMember(fieldName) || !jsonValue[fieldName].IsString())
  {
    return "";
  }
  return jsonValue[fieldName].GetString();
}

bool TeleBoy::ReadDataJson()
{
  if (!XBMC->FileExists(data_file, true))
  {
    return true;
  }
  std::string jsonString = Utils::ReadFile(data_file);
  if (jsonString.empty())
  {
    XBMC->Log(LOG_ERROR, "Loading data.json failed.");
    return false;
  }

  Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    XBMC->Log(LOG_ERROR, "Parsing data.json failed.");
    return false;
  }

  if (doc.HasMember("cinergy_s"))
  {
    cinergySCookies = GetStringOrEmpty(doc, "cinergy_s");
    XBMC->Log(LOG_DEBUG, "Loaded cinergy_s: %s..", cinergySCookies.substr(0, 5).c_str());
  }

  XBMC->Log(LOG_DEBUG, "Loaded data.json.");
  return true;
}

bool TeleBoy::WriteDataJson()
{
  void* file;
  if (!(file = XBMC->OpenFileForWrite(data_file, true)))
  {
    XBMC->Log(LOG_ERROR, "Save data.json failed.");
    return false;
  }

  Document d;
  d.SetObject();
  Document::AllocatorType& allocator = d.GetAllocator();
  
  if (!cinergySCookies.empty())
  {
    Value cinergySValue;
    cinergySValue.SetString(cinergySCookies.c_str(), cinergySCookies.length(), allocator);
    d.AddMember("cinergy_s", cinergySValue, allocator);
  }

  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  d.Accept(writer);
  const char* output = buffer.GetString();
  XBMC->WriteFile(file, output, strlen(output));
  XBMC->CloseFile(file);
  return true;
}

std::string TeleBoy::GetStreamParameters() {
  std::string params = enableDolby ? "&dolby=1" : "";
  params += "&streamformat=dash";
  return params;
}
