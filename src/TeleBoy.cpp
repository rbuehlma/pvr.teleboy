#include <algorithm>
#include <iostream>
#include <string>
#include "TeleBoy.h"
#include "yajl/yajl_tree.h"
#include "yajl/yajl_gen.h"
#include <sstream>
#include "p8-platform/sockets/tcp.h"
#include <map>
#include <time.h>
#include <random>
#include "Utils.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
 #pragma comment(lib, "ws2_32.lib")
  #include <stdio.h>
 #include <stdlib.h>
#endif

#ifdef TARGET_ANDROID
#include "to_string.h"
#endif

#define DEBUG

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif



using namespace ADDON;
using namespace std;

static const string tbUrl = "https://www.teleboy.ch";
static const string apiUrl = "http://tv.api.teleboy.ch";
static const string apiKey = "69d4547562510efe1b5d354bc34656fb34366b6c1023739ce46958007bf17ee9";
static const string apiDeviceType = "desktop";
static const string apiVersion = "1.5";

string TeleBoy::HttpGet(string url) {
  return HttpPost(url, "");
}

string TeleBoy::HttpPost(string url, string postData) {
  int statusCode;
  XBMC->Log(LOG_DEBUG, "Http-Request: %s.", url.c_str());
  string content = curl->Post(url, postData, statusCode);
  return content;
}

void TeleBoy::ApiSetHeader() {
  curl->AddHeader("x-teleboy-apikey", apiKey);
  curl->AddHeader("x-teleboy-device-type", apiDeviceType);
  curl->AddHeader("x-teleboy-session", curl->GetSessionId());
  curl->AddHeader("x-teleboy-version", apiVersion);
}

yajl_val TeleBoy::ApiGetResult(string content) {
  yajl_val json = JsonParser::parse(content);
  if (json != NULL) {
    if (JsonParser::getBoolean(json, 1, "success")) {
      return json;
    }
    yajl_tree_free(json);
  }
  return NULL;
}

yajl_val TeleBoy::ApiGet(string url) {
  return ApiPost(url, "");
}

yajl_val TeleBoy::ApiPost(string url, string postData) {
  ApiSetHeader();
  curl->AddHeader("Content-Type", "application/json");
  string content = HttpPost(apiUrl + url, postData);
  curl->ResetHeaders();
  return ApiGetResult(content);
}

yajl_val TeleBoy::ApiDelete(string url) {
  ApiSetHeader();
  string content = curl->Delete(apiUrl + url);
  curl->ResetHeaders();
  return ApiGetResult(content);
}

TeleBoy::TeleBoy(bool favoritesOnly) :
  username(""),
  password(""),
  maxRecallSeconds(60*60*24*7)
{
  curl = new Curl();
  this->favoritesOnly = favoritesOnly;
}

TeleBoy::~TeleBoy() {
  delete curl;
}

bool TeleBoy::Login(string u, string p) {
  HttpGet(tbUrl + "/login");
  string result = HttpPost(tbUrl + "/login_check", "login=" + u +"&password=" + p + "&keep_login=1");
  bool loginOk = result.find("Anmeldung war nicht erfolgreich") == std::string::npos &&
         result.find("Falsche Eingaben") == std::string::npos;
  if (!loginOk) {
    return false;
  }
  int pos = result.find("userSettings =");
  if (pos == std::string::npos) {
    return false;
  }
  int endPos = result.find("};", pos);
  string settings = result.substr(pos, endPos - pos);
  pos = settings.find(" id:") + 5;
  endPos = settings.find(",", pos);
  userId = settings.substr(pos, endPos-pos);
  return true;
}

void TeleBoy::GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities) {
  pCapabilities->bSupportsRecordings      = true;
  pCapabilities->bSupportsTimers          = true;
}

bool TeleBoy::LoadChannels() {
  yajl_val json = ApiGet("/epg/stations?expand=logos&language=de");
  if (json == NULL) {
    XBMC->Log(LOG_ERROR, "Error loading channels.");
    return false;
  }
  yajl_val channels = JsonParser::getArray(json, 2, "data", "items");
  for ( int index = 0; index < channels->u.array.len; ++index ) {
    yajl_val c = channels->u.array.values[index];
    if (!JsonParser::getBoolean(c, 1, "has_stream")) {
      continue;
    }
    TeleBoyChannel channel;
    channel.id = JsonParser::getInt(c, 1, "id");
    channel.name = JsonParser::getString(c, 1, "name");
    channel.logoPath = "https://media.cinergy.ch/t_station/"+ to_string(channel.id) +"/icon320_dark.png";
    channelsById[channel.id] = channel;
  }
  yajl_tree_free(json);

  json = ApiGet("/users/" + userId + "/stations?expand=logos&language=de");
  if (json == NULL) {
    XBMC->Log(LOG_ERROR, "Error loading sorted channels.");
    return false;
  }
  channels = JsonParser::getArray(json, 2, "data", "items");
  for ( int index = 0; index < channels->u.array.len; ++index ) {
    int cid = YAJL_GET_INTEGER(channels->u.array.values[index]);
    if (channelsById.find(cid) != channelsById.end()) {
      sortedChannels.push_back(cid);
    }
  }
  yajl_tree_free(json);
  return true;
}

int TeleBoy::GetChannelsAmount(void) {
  if (favoritesOnly) {
    return sortedChannels.size();
  }
  return channelsById.size();
}

PVR_ERROR TeleBoy::GetChannels(ADDON_HANDLE handle, bool bRadio) {
  int channelNum = 0;
  for(int const &cid : sortedChannels) {
    channelNum++;
    TransferChannel(handle, channelsById[cid], channelNum);
  }
  if (!favoritesOnly) {
    for(auto const &item : channelsById) {
      if (std::find(sortedChannels.begin(), sortedChannels.end(), item.first) != sortedChannels.end()) {
        continue;
      }
      channelNum++;
      TransferChannel(handle, item.second, channelNum);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

void TeleBoy::TransferChannel(ADDON_HANDLE handle, TeleBoyChannel channel, int channelNum) {
  PVR_CHANNEL kodiChannel;
  memset(&kodiChannel, 0, sizeof(PVR_CHANNEL));

  kodiChannel.iUniqueId = channel.id;
  kodiChannel.bIsRadio = false;
  kodiChannel.iChannelNumber = channelNum;
  PVR_STRCPY(kodiChannel.strChannelName, channel.name.c_str());
  PVR_STRCPY(kodiChannel.strIconPath, channel.logoPath.c_str());
  PVR_STRCPY(kodiChannel.strStreamURL, "pvr://stream/tv/teleboy.ts");
  PVR->TransferChannelEntry(handle, &kodiChannel);
}



string TeleBoy::GetChannelStreamUrl(int uniqueId) {
  yajl_val json = ApiGet("/users/" + userId + "/stream/live/" + to_string(uniqueId) + "?alternative=false");
  if (json == NULL) {
    XBMC->Log(LOG_ERROR, "Error getting live stream url for channel %i.", uniqueId);
    return "";
  }
  string url = JsonParser::getString(json, 3, "data", "stream", "url");
  XBMC->Log(LOG_ERROR, "Play URL: %s.", url.c_str());
  yajl_tree_free(json);
  return url;
}

PVR_ERROR TeleBoy::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd) {
  int totals = -1;
  int sum = 0;
  while (totals == -1 || sum < totals) {
    yajl_val json = ApiGet("/users/" + userId + "/broadcasts?begin="+ formatDateTime(iStart) +"&end="+ formatDateTime(iEnd) +"&expand=logos&limit=500&skip=" + to_string(sum) + "&sort=station&station=" + to_string(channel.iUniqueId));
    if (json == NULL) {
      XBMC->Log(LOG_ERROR, "Error getting epg for channel %i.", channel.iUniqueId);
      return PVR_ERROR_SERVER_ERROR;
    }
    totals = JsonParser::getInt(json, 2, "data", "total");
    yajl_val items = JsonParser::getArray(json, 2, "data", "items");
    for ( int index = 0; index < items->u.array.len; ++index ) {
      sum++;
      yajl_val item = items->u.array.values[index];
      EPG_TAG tag;
      memset(&tag, 0, sizeof(EPG_TAG));
      tag.iUniqueBroadcastId  = JsonParser::getInt(item, 1, "id");
      tag.strTitle            = strdup(JsonParser::getString(item, 1, "title").c_str());
      tag.iChannelNumber      = channel.iUniqueId;
      tag.startTime           = JsonParser::getTime(item, 1, "begin");
      tag.endTime             = JsonParser::getTime(item, 1, "end");
      tag.strPlotOutline      = strdup(JsonParser::getString(item, 1, "short_description").c_str());
      tag.strPlot             = strdup(JsonParser::getString(item, 1, "short_description").c_str());
      tag.strOriginalTitle    = strdup(JsonParser::getString(item, 1, "original_title").c_str());
      tag.strCast             = NULL;  /* not supported */
      tag.strDirector         = NULL;  /*SA not supported */
      tag.strWriter           = NULL;  /* not supported */
      tag.iYear               = JsonParser::getInt(item, 1, "year");
      tag.strIMDBNumber       = NULL;  /* not supported */
      tag.strIconPath         = NULL;  /* not supported */
      tag.iParentalRating     = 0;     /* not supported */
      tag.iStarRating         = 0;     /* not supported */
      tag.bNotify             = false; /* not supported */
      tag.iSeriesNumber       = 0;     /* not supported */
      tag.iEpisodeNumber      = 0;     /* not supported */
      tag.iEpisodePartNumber  = 0;     /* not supported */
      tag.strEpisodeName      = NULL;  /* not supported */
      tag.iFlags              = EPG_TAG_FLAG_UNDEFINED;

      PVR->TransferEpgEntry(handle, &tag);
    }
    XBMC->Log(LOG_DEBUG, "Loaded %i of %i epg entries for channel %i.", sum, totals, channel.iUniqueId);
    yajl_tree_free(json);
  }
  return PVR_ERROR_NO_ERROR;
}

string TeleBoy::formatDateTime(time_t dateTime) {
  char buff[20];
  strftime(buff, 20, "%Y-%m-%d+%H:%M:%S", localtime(&dateTime));
  return buff;
}

bool TeleBoy::Record(int programId) {
  string postData = "{\"broadcast\": " + to_string(programId) + ", \"alternative\": false}";
  yajl_val json = ApiPost("/users/" + userId + "/recordings", postData);
  if (json == NULL) {
    XBMC->Log(LOG_ERROR, "Error recording program %i.", programId);
    return false;
  }
  return true;
}

bool TeleBoy::DeleteRecording(string recordingId) {
  yajl_val json = ApiDelete("/users/" + userId + "/recordings/" + recordingId);
  if (json == NULL) {
    XBMC->Log(LOG_ERROR, "Error deleting recording %s.", recordingId.c_str());
    return false;
  }
  yajl_tree_free(json);
  return true;
}

void TeleBoy::GetRecordings(ADDON_HANDLE handle, string type) {
  int totals = -1;
  int sum = 0;
  while (totals == -1 || sum < totals) {
    yajl_val json = ApiGet("/users/" + userId + "/recordings/" + type +"?desc=1&expand=flags,logos&limit=100&skip=0&sort=date");
    if (json == NULL) {
      XBMC->Log(LOG_ERROR, "Error getting recordings of type %s.", type.c_str());
      return;
    }
    totals = JsonParser::getInt(json, 2, "data", "total");
    yajl_val items = JsonParser::getArray(json, 2, "data", "items");
    for ( int index = 0; index < items->u.array.len; ++index ) {
      sum++;
      yajl_val item = items->u.array.values[index];

      if (type.find("planned") == 0) {
        PVR_TIMER tag;
        memset(&tag, 0, sizeof(PVR_TIMER));

        tag.iClientIndex = JsonParser::getInt(item, 1, "id");
        PVR_STRCPY(tag.strTitle, JsonParser::getString(item, 1, "title").c_str());
        PVR_STRCPY(tag.strSummary, JsonParser::getString(item, 1, "subtitle").c_str());
        tag.startTime = JsonParser::getTime(item, 1, "begin");
        tag.endTime = JsonParser::getTime(item, 1, "end");
        tag.state = PVR_TIMER_STATE_SCHEDULED;
        tag.iTimerType = 1;
        tag.iEpgUid = JsonParser::getInt(item, 1, "id");
        tag.iClientChannelUid = JsonParser::getInt(item, 2, "station", "id");
        PVR->TransferTimerEntry(handle, &tag);

      } else {
        PVR_RECORDING tag;
        memset(&tag, 0, sizeof(PVR_RECORDING));
        tag.bIsDeleted = false;
        PVR_STRCPY(tag.strRecordingId, to_string(JsonParser::getInt(item, 1, "id")).c_str());
        PVR_STRCPY(tag.strTitle, JsonParser::getString(item, 1, "title").c_str());
        PVR_STRCPY(tag.strEpisodeName, JsonParser::getString(item, 1, "subtitle").c_str());
        PVR_STRCPY(tag.strPlot, JsonParser::getString(item, 1, "short_description").c_str());
        tag.iChannelUid = JsonParser::getInt(item, 2, "station", "id");
        tag.recordingTime = JsonParser::getTime(item, 1, "begin");
        time_t endTime = JsonParser::getTime(item, 1, "end");
        tag.iDuration = endTime -  tag.recordingTime;
        PVR_STRCPY(tag.strStreamURL, getRecordingStreamUrl(tag.strRecordingId).c_str());

        PVR->TransferRecordingEntry(handle, &tag);
      }
    }
    yajl_tree_free(json);
  }
}

string TeleBoy::getRecordingStreamUrl(string recordingId) {
  yajl_val json = ApiGet("/users/" + userId + "/stream/recording/" + recordingId);
  if (json == NULL) {
    XBMC->Log(LOG_ERROR, "Could not get URL for recording: %s.", recordingId.c_str());
    return "";
  }
  string url = JsonParser::getString(json, 3, "data", "stream", "url");
  yajl_tree_free(json);
  return url;
}

bool TeleBoy::IsPlayable(const EPG_TAG &tag) {
  time_t current_time;
  time(&current_time);
  bool timeOk = ((current_time - tag.endTime) < maxRecallSeconds) && (tag.startTime < current_time);
  if (timeOk && !GetEpgTagUrl(tag).empty()) {
    return true;
  }
  return false;
}

string TeleBoy::GetEpgTagUrl(const EPG_TAG &tag) {
  yajl_val json = ApiGet("/users/" + userId + "/stream/replay/" + to_string(tag.iUniqueBroadcastId));
  if (json == NULL) {
    XBMC->Log(LOG_ERROR, "Could not get URL for epg tag.");
    return "";
  }
  string url = JsonParser::getString(json, 3, "data", "stream", "url");
  yajl_tree_free(json);
  return url;
}
