#include "TeleBoy.h"
#include "Cache.h"
#include "md5.h"
#include "Utils.h"
#ifdef TARGET_WINDOWS
#include "windows.h"
#endif

#include <algorithm>
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <time.h>
#include <random>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "kodi/General.h"
#include "kodi/Filesystem.h"

#ifdef TARGET_ANDROID
#include "to_string.h"
#endif

using namespace std;
using namespace rapidjson;

static const string apiUrl = "https://tv.api.teleboy.ch";
static const string apiDeviceType = "desktop";
static const string apiVersion = "2.0";
const char data_file[] = "special://profile/addon_data/pvr.teleboy/data.json";
std::mutex TeleBoy::sendEpgToKodiMutex;
static const std::string user_agent = std::string("Kodi/")
    + std::string(STR(KODI_VERSION)) + std::string(" pvr.teleboy/")
    + std::string(STR(TELEBOY_VERSION)) + std::string(" (Kodi PVR addon)");


std::string TeleBoy::HttpGetCached(Curl &curl, const std::string& url, time_t cacheDuration)
{

  std::string content;
  std::string cacheKey = md5(url);
  if (!Cache::Read(cacheKey, content))
  {
    content = HttpGet(curl, url);
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
  curl.AddHeader("User-Agent", user_agent);
  int statusCode;
  kodi::Log(ADDON_LOG_DEBUG, "Http-Request: %s %s.", action.c_str(), url.c_str());
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

bool TeleBoy::ApiGet(string url, Document &doc, time_t timeout)
{
  Curl curl;
  ApiSetHeader(curl);
  string content;
  if (timeout > 0) {
    content = HttpGetCached(curl, apiUrl + url, timeout);
  } else {
    content = HttpGet(curl, apiUrl + url);
  }
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

TeleBoy::TeleBoy() :
    teleboyUsername(""), teleboyPassword(""), maxRecallSeconds(60 * 60 * 24 * 7), cinergySCookies(
        ""), isPlusMember(false), isComfortMember(false)
{
  kodi::Log(ADDON_LOG_INFO, "Using useragent: %s", user_agent.c_str());
  ReadDataJson();
}

TeleBoy::~TeleBoy()
{
  for (auto const &updateThread : updateThreads)
  {
    delete updateThread;
  }
}

ADDON_STATUS TeleBoy::Create()
{
  kodi::Log(ADDON_LOG_DEBUG, "%s - Creating the PVR Teleboy add-on", __FUNCTION__);

  favoritesOnly = kodi::GetSettingBoolean("favoritesonly");
  enableDolby = kodi::GetSettingBoolean("enableDolby");
  teleboyUsername = kodi::GetSettingString("username");
  teleboyPassword = kodi::GetSettingString("password");

  if (teleboyUsername.empty() || teleboyPassword.empty())
  {
    kodi::Log(ADDON_LOG_INFO, "Username or password not set.");
    kodi::QueueNotification(QUEUE_WARNING, "", kodi::GetLocalizedString(30100));
    return ADDON_STATUS_NEED_SETTINGS;
  }

  kodi::Log(ADDON_LOG_DEBUG, "Login Teleboy");
  if (Login(teleboyUsername, teleboyPassword))
  {
    kodi::Log(ADDON_LOG_DEBUG, "Login done");
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Login failed");
    kodi::QueueNotification(QUEUE_ERROR, "", kodi::GetLocalizedString(30101));
    return ADDON_STATUS_NEED_SETTINGS;
  }

  return ADDON_STATUS_OK;
}

ADDON_STATUS TeleBoy::SetSetting(const std::string& settingName, const kodi::CSettingValue& settingValue)
{
  if (settingName == "username")
  {
    string username = settingValue.GetString();
    if (username != teleboyUsername)
    {
      teleboyUsername = username;
      return ADDON_STATUS_NEED_RESTART;
    }
  }

  if (settingName == "password")
  {
    string password = settingValue.GetString();
    if (password != teleboyPassword)
    {
      teleboyPassword = password;
      return ADDON_STATUS_NEED_RESTART;
    }
  }

  if (settingName == "favoritesonly")
  {
    bool favOnly = settingValue.GetBoolean();
    if (favOnly != favoritesOnly)
    {
      favoritesOnly = favOnly;
      return ADDON_STATUS_NEED_RESTART;
    }
  }

  return ADDON_STATUS_OK;
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
    kodi::Log(ADDON_LOG_INFO, "Not yet authenticated. Try to login.");
    HttpGet(curl, tbUrl + "/login");
    string location = curl.GetLocation();
    if (location.find("t.teleboy.ch") != string::npos)
    {
      kodi::Log(ADDON_LOG_INFO, "Using t.teleboy.ch.");
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
      kodi::Log(ADDON_LOG_ERROR, "Failed to login.");
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
    return false;
  }
  size_t endPos = result.find("'", pos1);
  if (endPos - pos1 > 65 || endPos <= pos)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Got HTML body: %s", result.c_str());
    kodi::Log(ADDON_LOG_ERROR, "Received api key is invalid.");
    return false;
  }
  apiKey = result.substr(pos1, endPos - pos1);

  pos = result.find("setId(");
  if (pos == std::string::npos)
  {
    kodi::Log(ADDON_LOG_ERROR, "No user settings found.");
    return false;
  }
  pos += 6;
  endPos = result.find(")", pos);
  if (endPos - pos > 15 || endPos <= pos)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Got HTML body: %s", result.c_str());
    kodi::Log(ADDON_LOG_ERROR, "Received userId is invalid.");
    return false;
  }
  userId = result.substr(pos, endPos - pos);

  isPlusMember = result.find("setIsPlusMember(1", endPos) != std::string::npos;
  isComfortMember = result.find("setIsComfortMember(1", endPos)
      != std::string::npos;
  if (!isPlusMember) {
    kodi::Log(ADDON_LOG_INFO, "Free accounts are not supported.", userId.c_str());
    kodi::QueueNotification(QUEUE_ERROR, "", kodi::GetLocalizedString(30102));
    return false;
  }
  kodi::Log(ADDON_LOG_DEBUG, "Got userId: %s.", userId.c_str());

  for (int i = 0; i < 3; ++i)
  {
    updateThreads.emplace_back(new UpdateThread(i, *this));
  }

  LoadChannels();
  LoadGenres();
  return true;
}

PVR_ERROR TeleBoy::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(false);
  capabilities.SetSupportsChannelGroups(false);
  capabilities.SetSupportsRecordingPlayCount(false);
  capabilities.SetSupportsLastPlayedPosition(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetSupportsEPGEdl(true);
  capabilities.SetSupportsRecordingEdl(true);
  capabilities.SetSupportsRecordings(true);
  capabilities.SetSupportsTimers(true);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetBackendName(std::string& name)
{
  name = "Teleboy PVR Add-on";
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetBackendVersion(std::string& version)
{
  version = STR(TELEBOY_VERSION);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

void TeleBoy::LoadGenres()
{
  Document json;
  if (!ApiGet("/epg/genres", json, 3600))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error loading genres.");
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
  if (!ApiGet("/epg/stations?expand=logos&language=de", json, 3600))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error loading channels.");
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

  if (!ApiGet("/users/" + userId + "/stations", json, 3600))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error loading sorted channels.");
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

PVR_ERROR TeleBoy::GetChannelsAmount(int& amount)
{
  if (favoritesOnly)
  {
    amount = sortedChannels.size();
  }
  else
  {
    amount = channelsById.size();
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  int channelNum = 0;
  for (int const &cid : sortedChannels)
  {
    channelNum++;
    TransferChannel(results, channelsById[cid], channelNum);
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
      TransferChannel(results, item.second, channelNum);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

void TeleBoy::TransferChannel(kodi::addon::PVRChannelsResultSet& results, TeleBoyChannel channel,
    int channelNum)
{
  kodi::addon::PVRChannel kodiChannel;

  kodiChannel.SetUniqueId(channel.id);
  kodiChannel.SetIsRadio(false);
  kodiChannel.SetChannelNumber(channelNum);
  kodiChannel.SetChannelName(channel.name);
  kodiChannel.SetIconPath(channel.logoPath);

  results.Add(kodiChannel);
}

void TeleBoy::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties, const std::string& url, bool realtime)
{
  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back("inputstream.adaptive.manifest_type", "mpd");
  properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");
}

PVR_ERROR TeleBoy::GetChannelStreamProperties(const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;

  Document json;
  if (!ApiGet(
      "/users/" + userId + "/stream/live/" + to_string(channel.GetUniqueId())
          + "?expand=primary_image,flags&https=1" + GetStreamParameters(), json, 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error getting live stream url for channel %i.",
        channel.GetUniqueId());
    return ret;
  }
  string url = GetStringOrEmpty(json["data"]["stream"], "url");
  kodi::Log(ADDON_LOG_INFO, "Play URL: %s.", url.c_str());
  url = FollowRedirect(url);

  if (!url.empty())
  {
    SetStreamProperties(properties, url, true);
    ret = PVR_ERROR_NO_ERROR;
  }
  return ret;
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
      kodi::Log(ADDON_LOG_DEBUG, "Final url : %s.", currUrl.c_str());
      return currUrl;
    }
    kodi::Log(ADDON_LOG_DEBUG, "Redirected to : %s.", nextUrl.c_str());
    currUrl = nextUrl;
  }
  return currUrl;
}

PVR_ERROR TeleBoy::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  UpdateThread::LoadEpg(channelUid, start, end);
  return PVR_ERROR_NO_ERROR;
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
        "/users/" + userId + "/broadcasts?begin=" + FormatDate(iStart)
            + "+00:00:00&end=" + FormatDate(iEnd + 60 * 60 * 24) + "+00:00:00&expand=logos&limit=500&skip="
            + to_string(sum) + "&sort=station&station="
            + to_string(uniqueChannelId), json, 60*60*24))
    {
      kodi::Log(ADDON_LOG_ERROR, "Error getting epg for channel %i.",
          uniqueChannelId);
      return;
    }
    totals = json["data"]["total"].GetInt();
    const Value& items = json["data"]["items"];

    std::lock_guard<std::mutex> lock(sendEpgToKodiMutex);

    for (Value::ConstValueIterator itr1 = items.Begin(); itr1 != items.End();
        ++itr1)
    {
      const Value& item = (*itr1);
      sum++;
      kodi::addon::PVREPGTag tag;

      tag.SetUniqueBroadcastId(item["id"].GetInt());
      tag.SetTitle(GetStringOrEmpty(item, "title"));
      tag.SetUniqueChannelId(uniqueChannelId);
      tag.SetStartTime(Utils::StringToTime(GetStringOrEmpty(item, "begin")));
      tag.SetEndTime(Utils::StringToTime(GetStringOrEmpty(item, "end")));
      tag.SetPlotOutline(GetStringOrEmpty(item, "headline"));
      tag.SetPlot(GetStringOrEmpty(item, "short_description"));
      tag.SetOriginalTitle(GetStringOrEmpty(item, "original_title"));
      tag.SetCast(""); /* not supported */
      tag.SetDirector(""); /*SA not supported */
      tag.SetWriter(""); /* not supported */
      tag.SetYear(item.HasMember("year") ? item["year"].GetInt() : 0);
      tag.SetIMDBNumber(""); /* not supported */
      tag.SetIconPath(""); /* not supported */
      tag.SetParentalRating(0); /* not supported */
      tag.SetStarRating(0); /* not supported */
      tag.SetSeriesNumber(
          item.HasMember("serie_season") ? item["serie_season"].GetInt() : EPG_TAG_INVALID_SERIES_EPISODE);
      tag.SetEpisodeNumber(
          item.HasMember("serie_episode") ? item["serie_episode"].GetInt() : EPG_TAG_INVALID_SERIES_EPISODE);
      tag.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE); /* not supported */
      tag.SetEpisodeName(GetStringOrEmpty(item, "subtitle"));
      if (item.HasMember("genre_id")) {
        int genreId = item["genre_id"].GetInt();
        TeleboyGenre genre = genresById[genreId];
        int kodiGenre = m_categories.Category(genre.nameEn);
        if (kodiGenre == 0) {
          tag.SetGenreType(EPG_GENRE_USE_STRING);
          tag.SetGenreSubType(0);
          tag.SetGenreDescription(genre.name);
        } else {
          tag.SetGenreSubType(kodiGenre & 0x0F);
          tag.SetGenreType(kodiGenre & 0xF0);
        }
      }
      tag.SetFlags(EPG_TAG_FLAG_UNDEFINED);

      EpgEventStateChange(tag, EPG_EVENT_CREATED);
    }
    kodi::Log(ADDON_LOG_DEBUG, "Loaded %i of %i epg entries for channel %i.", sum,
        totals, uniqueChannelId);
  }
  return;
}

string TeleBoy::FormatDate(time_t dateTime)
{
  char buff[20];
  struct tm tm;
  gmtime_r(&dateTime, &tm);
  strftime(buff, 20, "%Y-%m-%d", &tm);
  return buff;
}

PVR_ERROR TeleBoy::GetRecordingsAmount(bool deleted, int& amount)
{
  amount = 0;
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR TeleBoy::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  Document doc;
  if (!ApiDelete("/users/" + userId + "/recordings/" + recording.GetRecordingId(), doc))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error deleting recording %s.", recording.GetRecordingId().c_str());
    return PVR_ERROR_SERVER_ERROR;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  int totals = -1;
  int sum = 0;
  string type = "ready";
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + userId + "/recordings/" + type
            + "?desc=1&expand=flags,logos&limit=100&skip=" + to_string(sum) + "&sort=date", json, 10))
    {
      kodi::Log(ADDON_LOG_ERROR, "Error getting recordings of type %s.",
          type.c_str());
      return PVR_ERROR_SERVER_ERROR;
    }
    totals = json["data"]["total"].GetInt();
    const Value& items = json["data"]["items"];
    for (Value::ConstValueIterator itr1 = items.Begin(); itr1 != items.End();
        ++itr1)
    {
      const Value& item = (*itr1);
      sum++;

      kodi::addon::PVRRecording tag;

      tag.SetSeriesNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
      tag.SetEpisodeNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
      tag.SetIsDeleted(false);
      tag.SetRecordingId(to_string(item["id"].GetInt()));
      tag.SetTitle(GetStringOrEmpty(item, "title"));
      tag.SetEpisodeName(GetStringOrEmpty(item, "subtitle"));
      tag.SetPlot(GetStringOrEmpty(item, "description"));
      tag.SetPlotOutline(GetStringOrEmpty(item, "short_description"));
      tag.SetChannelUid(item["station_id"].GetInt());
      tag.SetIconPath(channelsById[tag.GetChannelUid()].logoPath);
      tag.SetChannelName(channelsById[tag.GetChannelUid()].name);
      tag.SetRecordingTime(Utils::StringToTime(GetStringOrEmpty(item, "begin")));
      time_t endTime = Utils::StringToTime(GetStringOrEmpty(item, "end"));
      tag.SetDuration(endTime - tag.GetRecordingTime());
      tag.SetEPGEventId(item["id"].GetInt());
      if (item.HasMember("genre_id")) {
        int genreId = item["genre_id"].GetInt();
        TeleboyGenre genre = genresById[genreId];
        int kodiGenre = m_categories.Category(genre.nameEn);
        if (kodiGenre == 0) {
          tag.SetGenreType(EPG_GENRE_USE_STRING);
          tag.SetGenreSubType(0);
          tag.SetGenreDescription(genre.name);
        } else {
          tag.SetGenreSubType(kodiGenre & 0x0F);
          tag.SetGenreType(kodiGenre & 0xF0);
        }
      }

      results.Add(tag);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetRecordingStreamProperties(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;

  Document json;
  if (!ApiGet("/users/" + userId + "/stream/" + recording.GetRecordingId() + "?" + GetStreamParameters(), json, 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not get URL for recording: %s.",
        recording.GetRecordingId().c_str());
    return ret;
  }
  string url = GetStringOrEmpty(json["data"]["stream"], "url");
  url = FollowRedirect(url);

  if (!url.empty())
  {
    SetStreamProperties(properties, url, false);
    ret = PVR_ERROR_NO_ERROR;
  }
  return ret;
}

PVR_ERROR TeleBoy::GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl)
{
  kodi::addon::PVREDLEntry entry;
  entry.SetStart(0);
  entry.SetEnd(300000);
  entry.SetType(PVR_EDL_TYPE_COMBREAK);
  edl.emplace_back(entry);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  AddTimerType(types, 0, PVR_TIMER_TYPE_ATTRIBUTE_NONE);
  AddTimerType(types, 1, PVR_TIMER_TYPE_IS_MANUAL);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetTimersAmount(int& amount)
{
  amount = 0;
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR TeleBoy::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  int totals = -1;
  int sum = 0;
  string type = "planned";
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + userId + "/recordings/" + type
            + "?desc=1&expand=flags,logos&limit=100&skip=" + to_string(sum) + "&sort=date", json, 10))
    {
      kodi::Log(ADDON_LOG_ERROR, "Error getting recordings of type %s.",
          type.c_str());
      return PVR_ERROR_SERVER_ERROR;
    }
    totals = json["data"]["total"].GetInt();
    const Value& items = json["data"]["items"];
    for (Value::ConstValueIterator itr1 = items.Begin(); itr1 != items.End();
        ++itr1)
    {
      const Value& item = (*itr1);
      sum++;

      kodi::addon::PVRTimer tag;

      tag.SetClientIndex(item["id"].GetInt());
      tag.SetTitle(GetStringOrEmpty(item, "title"));
      tag.SetSummary(GetStringOrEmpty(item, "subtitle"));
      tag.SetStartTime(Utils::StringToTime(GetStringOrEmpty(item, "begin")));
      tag.SetEndTime(Utils::StringToTime(GetStringOrEmpty(item, "end")));
      tag.SetState(PVR_TIMER_STATE_SCHEDULED);
      tag.SetTimerType(1);
      tag.SetEPGUid(item["id"].GetInt());
      tag.SetClientChannelUid(item["station_id"].GetInt());
      if (item.HasMember("genre_id")) {
        int genreId = item["genre_id"].GetInt();
        TeleboyGenre genre = genresById[genreId];
        int kodiGenre = m_categories.Category(genre.nameEn);
        if (kodiGenre != 0) {
          tag.SetGenreSubType(kodiGenre & 0x0F);
          tag.SetGenreType(kodiGenre & 0xF0);
        }
      }

      results.Add(tag);
      UpdateThread::SetNextRecordingUpdate(tag.GetEndTime() + 60 * 21);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::AddTimer(const kodi::addon::PVRTimer& timer)
{
  if (timer.GetEPGUid() <= EPG_TAG_INVALID_UID)
  {
    return PVR_ERROR_REJECTED;
  }

  string postData = "{\"broadcast\": " + to_string(timer.GetEPGUid())
      + ", \"alternative\": false}";
  Document json;
  if (!ApiPost("/users/" + userId + "/recordings", postData, json))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error recording program %i.", timer.GetEPGUid());
    return PVR_ERROR_SERVER_ERROR;
  }

  kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
  kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  Document doc;
  if (!ApiDelete("/users/" + userId + "/recordings/" + to_string(timer.GetClientIndex()), doc))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error deleting timer %i.", timer.GetClientIndex());
    return PVR_ERROR_SERVER_ERROR;
  }

  kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
  kodi::addon::CInstancePVRClient::TriggerRecordingUpdate();
  return PVR_ERROR_NO_ERROR;
}

void TeleBoy::AddTimerType(std::vector<kodi::addon::PVRTimerType>& types, int idx, int attributes)
{
  kodi::addon::PVRTimerType type;
  type.SetId(static_cast<unsigned int>(idx + 1));
  type.SetAttributes(static_cast<unsigned int>(attributes));
  types.emplace_back(type);
}

PVR_ERROR TeleBoy::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& isPlayable)
{
  if (!isComfortMember && !isPlusMember)
  {
    isPlayable = false;
    return PVR_ERROR_NO_ERROR;
  }

  time_t current_time;
  time(&current_time);
  isPlayable = ((current_time - tag.GetEndTime()) < maxRecallSeconds)
      && (tag.GetStartTime() < current_time);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable)
{
  time_t current_time;
  time(&current_time);
  isRecordable = ((current_time - tag.GetEndTime()) < maxRecallSeconds);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;

  Document json;
  if (!ApiGet(
      "/users/" + userId + "/stream/"+ to_string(tag.GetUniqueBroadcastId()) + "?" + GetStreamParameters()
          , json, 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not get URL for epg tag.");
    return ret;
  }
  string url = GetStringOrEmpty(json["data"]["stream"], "url");
  url = FollowRedirect(url);
  if (!url.empty())
  {
    SetStreamProperties(properties, url, false);
    ret = PVR_ERROR_NO_ERROR;
  }
  return ret;
}

PVR_ERROR TeleBoy::GetEPGTagEdl(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVREDLEntry>& edl)
{
  kodi::addon::PVREDLEntry entry;
  entry.SetStart(0);
  entry.SetEnd(300000);
  entry.SetType(PVR_EDL_TYPE_COMBREAK);
  edl.emplace_back(entry);
  return PVR_ERROR_NO_ERROR;
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
  if (!kodi::vfs::FileExists(data_file, true))
  {
    return true;
  }
  std::string jsonString = Utils::ReadFile(data_file);
  if (jsonString.empty())
  {
    kodi::Log(ADDON_LOG_ERROR, "Loading data.json failed.");
    return false;
  }

  Document doc;
  doc.Parse(jsonString.c_str());
  if (doc.GetParseError())
  {
    kodi::Log(ADDON_LOG_ERROR, "Parsing data.json failed.");
    return false;
  }

  if (doc.HasMember("cinergy_s"))
  {
    cinergySCookies = GetStringOrEmpty(doc, "cinergy_s");
    kodi::Log(ADDON_LOG_DEBUG, "Loaded cinergy_s: %s..", cinergySCookies.substr(0, 5).c_str());
  }

  kodi::Log(ADDON_LOG_DEBUG, "Loaded data.json.");
  return true;
}

bool TeleBoy::WriteDataJson()
{
  kodi::vfs::CFile file;
  if (!file.OpenFileForWrite(data_file, true))
  {
    kodi::Log(ADDON_LOG_ERROR, "Save data.json failed.");
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
  file.Write(output, strlen(output));
  return true;
}

std::string TeleBoy::GetStreamParameters() {
  std::string params = enableDolby ? "&dolby=1" : "";
  params += "&https=1&streamformat=dash";
  return params;
}

ADDONCREATOR(TeleBoy)
