#include "client.h"
#include "TeleBoy.h"
#include "kodi/xbmc_pvr_dll.h"
#include "kodi/libKODI_guilib.h"
#include <chrono>
#include <thread>


using namespace ADDON;

#ifdef TARGET_WINDOWS
#define snprintf _snprintf
#endif

#ifdef TARGET_ANDROID
#include "to_string.h"
#endif

ADDON_STATUS m_CurStatus = ADDON_STATUS_UNKNOWN;
TeleBoy *teleboy = NULL;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string g_strUserPath = "";
std::string g_strClientPath = "";

CHelper_libXBMC_addon *XBMC = NULL;
CHelper_libXBMC_pvr *PVR = NULL;

std::string teleboyUsername = "";
std::string teleboyPassword = "";
bool teleboyFavoritesOnly = false;
bool teleboyEnableDolby = true;
int runningRequests = 0;

extern "C"
{

void ADDON_ReadSettings(void)
{
  char buffer[1024];
  bool boolBuffer;
  XBMC->Log(LOG_DEBUG, "Read settings");
  if (XBMC->GetSetting("username", &buffer))
  {
    teleboyUsername = buffer;
  }
  if (XBMC->GetSetting("password", &buffer))
  {
    teleboyPassword = buffer;
  }
  if (XBMC->GetSetting("favoritesonly", &boolBuffer))
  {
    teleboyFavoritesOnly = boolBuffer;
  }
  if (XBMC->GetSetting("enableDolby", &boolBuffer))
  {
    teleboyEnableDolby = boolBuffer;
  }
  XBMC->Log(LOG_DEBUG, "End Readsettings");
}

ADDON_STATUS ADDON_Create(void *hdl, void *props)
{
  if (!hdl || !props)
  {
    return ADDON_STATUS_UNKNOWN;
  }

  PVR_PROPERTIES *pvrprops = (PVR_PROPERTIES *) props;

  XBMC = new CHelper_libXBMC_addon;
  XBMC->RegisterMe(hdl);

  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_DEBUG, "%s - Creating the PVR Teleboy add-on", __FUNCTION__);

  m_CurStatus = ADDON_STATUS_NEED_SETTINGS;

  g_strClientPath = pvrprops->strClientPath;
  g_strUserPath = pvrprops->strUserPath;

  teleboyUsername = "";
  teleboyPassword = "";
  ADDON_ReadSettings();
  
  if (teleboyUsername.empty() || teleboyPassword.empty()) {
    XBMC->Log(LOG_NOTICE, "Username or password not set.");
    XBMC->QueueNotification(QUEUE_WARNING, XBMC->GetLocalizedString(30100));
    return m_CurStatus;
  }
  
  XBMC->Log(LOG_DEBUG, "Create Teleboy");
  teleboy = new TeleBoy(teleboyFavoritesOnly, teleboyEnableDolby);
  XBMC->Log(LOG_DEBUG, "Login Teleboy");
  if (teleboy->Login(teleboyUsername, teleboyPassword)) {
    XBMC->Log(LOG_DEBUG, "Login done");
    m_CurStatus = ADDON_STATUS_OK;
  } else {
    XBMC->Log(LOG_ERROR, "Login failed");
    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30101));
  }

  return m_CurStatus;
}

ADDON_STATUS ADDON_GetStatus()
{
  return m_CurStatus;
}

void ADDON_Destroy()
{
  TeleBoy *oldTeleboy = teleboy;
  teleboy = nullptr;
  
  int waitCount = 10;
  while (runningRequests > 0 && waitCount > 0)
  {
    XBMC->Log(LOG_NOTICE, "Wait for %d requests to finish for %d seconds.", runningRequests, waitCount);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    waitCount--;
  }

  SAFE_DELETE(oldTeleboy);
  SAFE_DELETE(PVR);
  SAFE_DELETE(XBMC);

  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  string name = settingName;

  if (name == "username")
  {
    string username = (const char*) settingValue;
    if (username != teleboyUsername)
    {
      teleboyUsername = username;
      return ADDON_STATUS_NEED_RESTART;
    }
  }

  if (name == "password")
  {
    string password = (const char*) settingValue;
    if (password != teleboyPassword)
    {
      teleboyPassword = password;
      return ADDON_STATUS_NEED_RESTART;
    }
  }

  if (name == "favoritesonly")
  {
    bool favOnly = *(bool *) settingValue;
    if (favOnly != teleboyFavoritesOnly)
    {
      teleboyFavoritesOnly = favOnly;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  
  return ADDON_STATUS_OK;
}

void ADDON_Stop()
{
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

void OnSystemSleep()
{
}

void OnSystemWake()
{
}

void OnPowerSavingActivated()
{
}

void OnPowerSavingDeactivated()
{
}

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG = true;
  pCapabilities->bSupportsTV = true;
  pCapabilities->bSupportsRadio = false;
  pCapabilities->bSupportsChannelGroups = false;
  pCapabilities->bSupportsRecordingPlayCount = false;
  pCapabilities->bSupportsLastPlayedPosition = false;
  pCapabilities->bSupportsRecordingsRename = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo = false;
  pCapabilities->bSupportsEPGEdl = true;
  pCapabilities->bSupportsRecordingEdl = true;

  runningRequests++;
  if (teleboy)
  {
    teleboy->GetAddonCapabilities(pCapabilities);
  }
  runningRequests--;
  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const char *strBackendName = "Teleboy PVR Add-on";
  return strBackendName;
}

const char *GetBackendVersion(void)
{
  static std::string strBackendVersion = STR(IPTV_VERSION);
  return strBackendVersion.c_str();
}

const char *GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

const char *GetBackendHostname(void)
{
  return "";
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, int iChannelUid,
    time_t iStart, time_t iEnd)
{
  runningRequests++;
  PVR_ERROR ret = PVR_ERROR_SERVER_ERROR;
  if (teleboy)
  {
    teleboy->GetEPGForChannel(iChannelUid, iStart, iEnd);
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;

  return ret;
}

int GetChannelsAmount(void)
{
  int ret = 0;
  runningRequests++;
  if (teleboy)
    ret = teleboy->GetChannelsAmount();
  
  runningRequests--;

  return ret;
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (bRadio)
    return PVR_ERROR_NO_ERROR;

  PVR_ERROR ret = PVR_ERROR_NO_ERROR;
  runningRequests++;
  if (teleboy)
    ret = teleboy->GetChannels(handle, bRadio);
  
  runningRequests--;

  return ret;
}

bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  return false;
}

void CloseLiveStream(void)
{

}

int GetCurrentClientChannel(void)
{
  return -1;
}

bool SwitchChannel(const PVR_CHANNEL &channel)
{
  CloseLiveStream();

  return OpenLiveStream(channel);
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES* pProperties)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

int GetChannelGroupsAmount(void)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle,
    const PVR_CHANNEL_GROUP &group)
{
  return PVR_ERROR_SERVER_ERROR;
}

void setStreamProperty(PVR_NAMED_VALUE* properties, unsigned int* propertiesCount, std::string name, std::string value)
{
  strncpy(properties[*propertiesCount].strName, name.c_str(), sizeof(properties[*propertiesCount].strName) - 1);
  strncpy(properties[*propertiesCount].strValue, value.c_str(), sizeof(properties[*propertiesCount].strValue) - 1);
  *propertiesCount = (*propertiesCount) + 1;
}

void setStreamProperties(PVR_NAMED_VALUE* properties, unsigned int* propertiesCount, std::string url)
{
  setStreamProperty(properties, propertiesCount, PVR_STREAM_PROPERTY_STREAMURL, url);
  setStreamProperty(properties, propertiesCount, PVR_STREAM_PROPERTY_INPUTSTREAMADDON, "inputstream.adaptive");
  setStreamProperty(properties, propertiesCount, "inputstream.adaptive.manifest_type", "mpd");
  setStreamProperty(properties, propertiesCount, "inputstream.adaptive.manifest_update_parameter", "full");
  setStreamProperty(properties, propertiesCount, PVR_STREAM_PROPERTY_MIMETYPE, "application/xml+dash");
}

PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL* channel,
    PVR_NAMED_VALUE* properties, unsigned int* propertiesCount)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;
  runningRequests++;
  std::string strUrl = teleboy->GetChannelStreamUrl(channel->iUniqueId);
  if (!strUrl.empty())
  {
    *propertiesCount = 0;
    setStreamProperties(properties, propertiesCount, strUrl);
    setStreamProperty(properties, propertiesCount, PVR_STREAM_PROPERTY_ISREALTIMESTREAM, "true");
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;
  return ret;
}

PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING* recording,
    PVR_NAMED_VALUE* properties, unsigned int* propertiesCount)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;
  runningRequests++;
  std::string strUrl = teleboy->GetRecordingStreamUrl(recording->strRecordingId);
  if (!strUrl.empty())
  {
    *propertiesCount = 0;
    setStreamProperties(properties, propertiesCount, strUrl);
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;
  return ret;
}

/** Recording API **/
int GetRecordingsAmount(bool deleted)
{
  return 0;
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
  if (deleted)
  {
    return PVR_ERROR_NO_ERROR;
  }
  PVR_ERROR ret = PVR_ERROR_SERVER_ERROR; 
  runningRequests++;
  if (teleboy && m_CurStatus == ADDON_STATUS_OK)
  {
    teleboy->GetRecordings(handle, "ready");
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;
  return ret;
}

int GetTimersAmount(void)
{
	return 0;
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  PVR_ERROR ret = PVR_ERROR_SERVER_ERROR;
  runningRequests++;
  if (teleboy && m_CurStatus == ADDON_STATUS_OK)
  {
    teleboy->GetRecordings(handle, "planned");
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;
  return ret;
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  if (timer.iEpgUid <= EPG_TAG_INVALID_UID)
  {
    return PVR_ERROR_REJECTED;
  }
  runningRequests++;
  PVR_ERROR ret = PVR_ERROR_SERVER_ERROR;
  if (teleboy)
  {
    ret = PVR_ERROR_REJECTED;
    if (teleboy->Record(timer.iEpgUid))
    {
      PVR->TriggerTimerUpdate();
      PVR->TriggerRecordingUpdate();
      ret = PVR_ERROR_NO_ERROR;
    }
  }
  runningRequests--;
  return ret;
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  PVR_ERROR ret = PVR_ERROR_SERVER_ERROR;
  runningRequests++;
  if (teleboy)
  {
    ret = PVR_ERROR_REJECTED;
    if (teleboy->DeleteRecording(recording.strRecordingId))
    {
      ret = PVR_ERROR_NO_ERROR;
    }
  }
  runningRequests--;
  return ret;
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  PVR_ERROR ret = PVR_ERROR_SERVER_ERROR;
  runningRequests++;
  if (teleboy)
  {
    ret = PVR_ERROR_REJECTED;
    if (teleboy->DeleteRecording(to_string(timer.iClientIndex)))
    {
      PVR->TriggerTimerUpdate();
      PVR->TriggerRecordingUpdate();
      ret = PVR_ERROR_NO_ERROR;
    }
  }
  runningRequests--;
  return ret;
}

void addTimerType(PVR_TIMER_TYPE types[], int idx, int attributes)
{
  types[idx].iId = idx + 1;
  types[idx].iAttributes = attributes;
  types[idx].iPrioritiesSize = 0;
  types[idx].iLifetimesSize = 0;
  types[idx].iPreventDuplicateEpisodesSize = 0;
  types[idx].iRecordingGroupSize = 0;
  types[idx].iMaxRecordingsSize = 0;
}

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  addTimerType(types, 0, PVR_TIMER_TYPE_ATTRIBUTE_NONE);
  addTimerType(types, 1, PVR_TIMER_TYPE_IS_MANUAL);
  *size = 2;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR IsEPGTagRecordable(const EPG_TAG* tag, bool* bIsRecordable)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;
  runningRequests++;
  if (teleboy)
  {
    *bIsRecordable = teleboy->IsRecordable(tag);
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;
  return ret;
}

PVR_ERROR IsEPGTagPlayable(const EPG_TAG* tag, bool* bIsPlayable)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;
  runningRequests++;
  if (teleboy)
  {
    *bIsPlayable = teleboy->IsPlayable(tag);
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;
  return ret;
}

PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG* tag,
    PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  PVR_ERROR ret = PVR_ERROR_FAILED;
  runningRequests++;
  std::string strUrl = teleboy->GetEpgTagUrl(tag);
  if (!strUrl.empty())
  {
    *iPropertiesCount = 0;
    setStreamProperties(properties, iPropertiesCount, strUrl);
    ret = PVR_ERROR_NO_ERROR;
  }
  runningRequests--;
  return ret;
}

PVR_ERROR GetEPGTagEdl(const EPG_TAG* epgTag, PVR_EDL_ENTRY edl[], int *size)
{
  edl[0].start=0;
  edl[0].end = 300000;
  edl[0].type = PVR_EDL_TYPE_COMBREAK;
  *size = 1;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording,
    int lastplayedposition)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
	return -1;
}

PVR_ERROR GetRecordingSize(const PVR_RECORDING* recording, int64_t* sizeInBytes)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

/** UNUSED API FUNCTIONS */
bool CanPauseStream(void)
{
  return true;
}
PVR_ERROR OpenDialogChannelScan(void)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook,
    const PVR_MENUHOOK_DATA &item)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
bool OpenRecordedStream(const PVR_RECORDING &recording)
{
  return false;
}
void CloseRecordedStream(void)
{
}
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return 0;
}
long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */)
{
  return 0;
}
long long LengthRecordedStream(void)
{
  return 0;
}
void DemuxReset(void)
{
}
void DemuxFlush(void)
{
}
int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return 0;
}
long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */)
{
  return -1;
}
long long LengthLiveStream(void)
{
  return -1;
}
PVR_ERROR RenameRecording(const PVR_RECORDING &recording)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR GetRecordingEdl(const PVR_RECORDING& recording, PVR_EDL_ENTRY edl[], int *size)
{
  edl[0].start=0;
  edl[0].end = 300000;
  edl[0].type = PVR_EDL_TYPE_COMBREAK;
  *size = 1;
  return PVR_ERROR_NO_ERROR;
}
PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
void DemuxAbort(void)
{
}
DemuxPacket* DemuxRead(void)
{
  return NULL;
}
void FillBuffer(bool mode) 
{
}
unsigned int GetChannelSwitchDelay(void)
{
  return 0;
}
bool IsRealTimeStream(void)
{
  return true;
}
void PauseStream(bool bPaused)
{
}
bool CanSeekStream(void)
{
  return true;
}
bool SeekTime(double, bool, double*)
{
  return false;
}
void SetSpeed(int)
{
}
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR DeleteAllRecordingsFromTrash()
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR SetEPGTimeFrame(int)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *times)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}
PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR GetStreamReadChunkSize(int* chunksize)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

}
