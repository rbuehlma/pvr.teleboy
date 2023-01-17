#include "TeleBoy.h"
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
std::mutex TeleBoy::sendEpgToKodiMutex;

bool TeleBoy::ApiGetResult(string content, Document &doc)
{
  doc.Parse(content.c_str());
  if (!doc.GetParseError())
  {
    if (doc["success"].GetBool())
    {
      return true;
    }
    if (doc["error_code"].GetInt() == 10403) {
      kodi::Log(ADDON_LOG_WARNING, "Got error_code 10403. Reset session.");
      m_session->Reset();
    }
  }
  return false;
}

bool TeleBoy::ApiGet(string url, Document &doc, time_t timeout)
{
  if (!m_session->IsConnected()) {
    return false;
  }
  return ApiGetWithoutConnectedCheck(url, doc, timeout);
}

bool TeleBoy::ApiGetWithoutConnectedCheck(string url, Document &doc, time_t timeout)
{
  string content;
  int statusCode;
  if (timeout > 0) {
    content = m_httpClient->HttpGetCached(apiUrl + url, timeout, statusCode);
  } else {
    content = m_httpClient->HttpGet(apiUrl + url, statusCode);
  }
  return ApiGetResult(content, doc);
}

bool TeleBoy::ApiPost(string url, string postData, Document &doc)
{
  int statusCode;
  if (!m_session->IsConnected()) {
    return false;
  }
  string content = m_httpClient->HttpPost(apiUrl + url, postData, statusCode);
  return ApiGetResult(content, doc);
}

bool TeleBoy::ApiDelete(string url, Document &doc)
{
  int statusCode;
  if (!m_session->IsConnected()) {
    return false;
  }
  string content = m_httpClient->HttpDelete(apiUrl + url, statusCode);
  return ApiGetResult(content, doc);
}

TeleBoy::TeleBoy()
{
  m_parameterDB = new ParameterDB(UserPath());
  m_httpClient = new HttpClient(m_parameterDB);
  m_session = new Session(m_httpClient, this);
  m_httpClient->SetStatusCodeHandler(m_session);
  
  UpdateConnectionState("Initializing", PVR_CONNECTION_STATE_CONNECTING, "");
}

TeleBoy::~TeleBoy()
{
  for (auto updateThread : updateThreads)
  {
    delete updateThread;
  }
  delete m_session;
  delete m_httpClient;
  delete m_parameterDB;
}

ADDON_STATUS TeleBoy::Create()
{
  kodi::Log(ADDON_LOG_DEBUG, "%s - Creating the PVR Teleboy add-on", __FUNCTION__);
  return m_session->Start();
}

ADDON_STATUS TeleBoy::SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
  return m_session->SetSetting(settingName, settingValue);
}

void TeleBoy::UpdateConnectionState(const std::string& connectionString, PVR_CONNECTION_STATE newState, const std::string& message) {
  kodi::addon::CInstancePVRClient::ConnectionStateChange(connectionString, newState, message);
}

bool TeleBoy::SessionInitialized()
{
  while (updateThreads.size() < 3)
  {
    updateThreads.emplace_back(new UpdateThread(updateThreads.size(), *this, *m_session));
  }

  LoadGenres();
  return LoadChannels();
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
  capabilities.SetSupportsRecordingsDelete(true);
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
  connection = m_session->IsConnected() ? "connected" : "not connected";
  return PVR_ERROR_NO_ERROR;
}

void TeleBoy::LoadGenres()
{
  Document json;
  if (!ApiGetWithoutConnectedCheck("/epg/genres", json, 3600))
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
  if (!ApiGetWithoutConnectedCheck("/epg/stations?expand=logos&language=de", json, 3600))
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

  if (!ApiGetWithoutConnectedCheck("/users/" + m_session->GetUserId() + "/stations", json, 3600))
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  if (m_session->GetFavoritesOnly())
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  int channelNum = 0;
  for (int const &cid : sortedChannels)
  {
    channelNum++;
    TransferChannel(results, channelsById[cid], channelNum);
  }
  if (!m_session->GetFavoritesOnly())
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

PVR_ERROR TeleBoy::SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties, const Value& stream, bool realtime)
{
  string url = GetStringOrEmpty(stream, "url");
  kodi::Log(ADDON_LOG_INFO, "Play URL: %s.", url.c_str());
  url = FollowRedirect(url);

  if (url.empty())
  {
    return PVR_ERROR_FAILED;
  }

  properties.emplace_back(PVR_STREAM_PROPERTY_STREAMURL, url);
  properties.emplace_back(PVR_STREAM_PROPERTY_INPUTSTREAM, "inputstream.adaptive");
  properties.emplace_back("inputstream.adaptive.manifest_type", "mpd");
  properties.emplace_back("inputstream.adaptive.manifest_update_parameter", "full");
  properties.emplace_back(PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");
  properties.emplace_back(PVR_STREAM_PROPERTY_ISREALTIMESTREAM, realtime ? "true" : "false");
      
  if (stream.HasMember("drm")) {
    string drmType = GetStringOrEmpty(stream["drm"], "type");
    if (drmType == "widevine") {
      string licenseUrl = GetStringOrEmpty(stream["drm"], "license_url");
      properties.emplace_back("inputstream.adaptive.license_key", licenseUrl + "||A{SSM}|");
      properties.emplace_back("inputstream.adaptive.license_type", "com.widevine.alpha"); 
    } else {
      kodi::Log(ADDON_LOG_ERROR, "Unsupported drm type: %s.", drmType.c_str());
    }      
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetChannelStreamProperties(const kodi::addon::PVRChannel& channel, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  Document json;
  if (!ApiGet(
      "/users/" + m_session->GetUserId() + "/stream/live/" + to_string(channel.GetUniqueId())
          + "?expand=primary_image,flags&https=1" + GetStreamParameters(), json, 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error getting live stream url for channel %i.",
        channel.GetUniqueId());
    return PVR_ERROR_FAILED;
  }
  const Value& stream = json["data"]["stream"];
  return SetStreamProperties(properties, stream, true);

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
        "/users/" + m_session->GetUserId() + "/broadcasts?begin=" + FormatDate(iStart)
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }
  Document doc;
  if (!ApiDelete("/users/" + m_session->GetUserId() + "/recordings/" + recording.GetRecordingId(), doc))
  {
    kodi::Log(ADDON_LOG_ERROR, "Error deleting recording %s.", recording.GetRecordingId().c_str());
    return PVR_ERROR_SERVER_ERROR;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  int totals = -1;
  int sum = 0;
  string type = "ready";
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + m_session->GetUserId() + "/recordings/" + type
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
      if (item.HasMember("serie_season")) {
        tag.SetSeriesNumber(item["serie_season"].GetInt());
        tag.SetDirectory(tag.GetTitle());
      }
      if (item.HasMember("serie_episode")) {
        tag.SetEpisodeNumber(item["serie_episode"].GetInt());
      }
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  PVR_ERROR ret = PVR_ERROR_FAILED;

  Document json;
  if (!ApiGet("/users/" + m_session->GetUserId() + "/stream/" + recording.GetRecordingId() + "?" + GetStreamParameters(), json, 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not get URL for recording: %s.",
        recording.GetRecordingId().c_str());
    return ret;
  }
  const Value& stream = json["data"]["stream"];
  return SetStreamProperties(properties, stream, false);
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  int totals = -1;
  int sum = 0;
  string type = "planned";
  while (totals == -1 || sum < totals)
  {
    Document json;
    if (!ApiGet(
        "/users/" + m_session->GetUserId() + "/recordings/" + type
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  if (timer.GetEPGUid() <= EPG_TAG_INVALID_UID)
  {
    return PVR_ERROR_REJECTED;
  }

  string postData = "{\"broadcast\": " + to_string(timer.GetEPGUid())
      + ", \"alternative\": false}";
  Document json;
  if (!ApiPost("/users/" + m_session->GetUserId() + "/recordings", postData, json))
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  Document doc;
  if (!ApiDelete("/users/" + m_session->GetUserId() + "/recordings/" + to_string(timer.GetClientIndex()), doc))
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
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  if (!m_session->GetIsPaidMember())
  {
    isPlayable = false;
    return PVR_ERROR_NO_ERROR;
  }

  time_t current_time;
  time(&current_time);
  isPlayable = ((current_time - tag.GetEndTime()) < m_session->GetMaxRecallSeconds())
      && (tag.GetStartTime() < current_time);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable)
{
  time_t current_time;
  time(&current_time);
  isRecordable = ((current_time - tag.GetEndTime()) < m_session->GetMaxRecallSeconds());
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TeleBoy::GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  if (!m_session->IsConnected()) {
    return PVR_ERROR_SERVER_ERROR;
  }

  PVR_ERROR ret = PVR_ERROR_FAILED;

  Document json;
  if (!ApiGet(
      "/users/" + m_session->GetUserId() + "/stream/"+ to_string(tag.GetUniqueBroadcastId()) + "?" + GetStreamParameters()
          , json, 0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not get URL for epg tag.");
    return ret;
  }
  const Value& stream = json["data"]["stream"];
  return SetStreamProperties(properties, stream, false);
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

std::string TeleBoy::GetStreamParameters() {
  std::string params = m_session->GetEnableDolby() ? "&dolby=1" : "";
  params += "&https=1&streamformat=dash";
  return params;
}

ADDONCREATOR(TeleBoy)
