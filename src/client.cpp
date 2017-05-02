#include "client.h"
#include "TeleBoy.h"
#include "kodi/xbmc_pvr_dll.h"
#include "kodi/libKODI_guilib.h"
#include <iostream>



using namespace ADDON;

#ifdef TARGET_WINDOWS
#define snprintf _snprintf
#endif

#ifdef TARGET_ANDROID
#include "to_string.h"
#endif


ADDON_STATUS m_CurStatus = ADDON_STATUS_UNKNOWN;
TeleBoy *teleboy = NULL;
bool m_bIsPlaying = false;
time_t g_pvrTimeShift;


/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string g_strUserPath   = "";
std::string g_strClientPath = "";

CHelper_libXBMC_addon *XBMC = NULL;
CHelper_libXBMC_pvr   *PVR  = NULL;

std::string teleboyUsername    = "";
std::string teleboyPassword    = "";
bool      teleboyFavoritesOnly = false;
int         g_iStartNumber  = 1;
bool        g_bTSOverride   = true;
bool        g_bCacheM3U     = false;
bool        g_bCacheEPG     = false;
int         g_iEPGLogos     = 0;



extern std::string PathCombine(const std::string &strPath, const std::string &strFileName)
{
    std::string strResult = strPath;
    if (strResult.at(strResult.size() - 1) == '\\' ||
        strResult.at(strResult.size() - 1) == '/')
    {
        strResult.append(strFileName);
    }
    else
    {
        strResult.append("/");
        strResult.append(strFileName);
    }

    return strResult;
}


extern std::string GetUserFilePath(const std::string &strFileName)
{
    return PathCombine(g_strUserPath, strFileName);
}

extern "C" {

void ADDON_ReadSettings(void) {
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
    XBMC->Log(LOG_DEBUG, "End Readsettings");
}

ADDON_STATUS ADDON_Create(void *hdl, void *props) {
    if (!hdl || !props) {
        return ADDON_STATUS_UNKNOWN;
    }

    g_pvrTimeShift = 0;

    PVR_PROPERTIES *pvrprops = (PVR_PROPERTIES *) props;

    XBMC = new CHelper_libXBMC_addon;
    XBMC->RegisterMe(hdl);

    if (!XBMC->RegisterMe(hdl)) {
        SAFE_DELETE(XBMC);
        return ADDON_STATUS_PERMANENT_FAILURE;
    }

    PVR = new CHelper_libXBMC_pvr;
    if (!PVR->RegisterMe(hdl)) {
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
    XBMC->Log(LOG_DEBUG, "Create Teleboy");
    teleboy = new TeleBoy(teleboyFavoritesOnly);
    XBMC->Log(LOG_DEBUG, "Teleboy created");
    if (!teleboyUsername.empty() && !teleboyPassword.empty()) {
      XBMC->Log(LOG_DEBUG, "Login Teleboy");
      if (teleboy->Login(teleboyUsername, teleboyPassword)) {
        XBMC->Log(LOG_DEBUG, "Login done");
        teleboy->LoadChannels();
        m_CurStatus = ADDON_STATUS_OK;
      } else {
        XBMC->Log(LOG_ERROR, "Login failed");
        XBMC->QueueNotification(QUEUE_ERROR,  XBMC->GetLocalizedString(37111));
      }
    }

    return m_CurStatus;
}

ADDON_STATUS ADDON_GetStatus() {
    return m_CurStatus;
}

void ADDON_Destroy() {
  SAFE_DELETE(teleboy);
  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

bool ADDON_HasSettings() {
    return true;
}

unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet) {
    return 0;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue) {
  string name = settingName;

  if (name == "username") {
    string username = (const char*)settingValue;
    if (username != teleboyUsername) {
      teleboyUsername = username;
      return ADDON_STATUS_NEED_RESTART;
    }
  }

  if (name == "password") {
    string password = (const char*)settingValue;
    if (password != teleboyPassword) {
      teleboyPassword = password;
      return ADDON_STATUS_NEED_RESTART;
    }
  }

  if (name == "favoritesonly") {
    bool favOnly = *(bool *)settingValue;
    if (favOnly != teleboyFavoritesOnly) {
      teleboyFavoritesOnly = favOnly;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  return !teleboyUsername.empty() && !teleboyPassword.empty() ? ADDON_STATUS_OK : ADDON_STATUS_NEED_SETTINGS;
}

void ADDON_Stop() {
}

void ADDON_FreeSettings() {
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

const char* GetPVRAPIVersion(void)
{
  static const char *strApiVersion = XBMC_PVR_API_VERSION;
  return strApiVersion;
}

const char* GetMininumPVRAPIVersion(void)
{

  static const char *strMinApiVersion = XBMC_PVR_MIN_API_VERSION;
  return strMinApiVersion;
}

const char* GetGUIAPIVersion(void)
{
    return KODI_GUILIB_API_VERSION;
}

const char* GetMininumGUIAPIVersion(void)
{
    return KODI_GUILIB_MIN_API_VERSION;
}

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG             = true;
  pCapabilities->bSupportsTV              = true;
  pCapabilities->bSupportsRadio           = false;
  pCapabilities->bSupportsChannelGroups   = false;
  pCapabilities->bSupportsRecordingPlayCount = false;
  pCapabilities->bSupportsLastPlayedPosition = false;

  if (teleboy) {
    teleboy->GetAddonCapabilities(pCapabilities);
  }

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const char *strBackendName = "Teleboy PVR Add-on";
  return strBackendName;
}

const char *GetBackendVersion(void)
{
  static std::string strBackendVersion = XBMC_PVR_API_VERSION;
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

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  *iTotal = 0;
  *iUsed  = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  if (teleboy)
    return teleboy->GetEPGForChannel(handle, channel, iStart, iEnd);

  return PVR_ERROR_SERVER_ERROR;
}

int GetChannelsAmount(void)
{
  if (teleboy)
    return teleboy->GetChannelsAmount();

  return 0;
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if(bRadio)
    return PVR_ERROR_NO_ERROR;

  if (teleboy)
    return teleboy->GetChannels(handle, bRadio);

  return PVR_ERROR_NO_ERROR;
}

bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  g_pvrTimeShift = 0;
  std::string url = GetLiveStreamURL(channel);
  XBMC->Log(LOG_DEBUG, "Open Livestream URL %s", url.c_str());
  return true;
}

void CloseLiveStream(void) { }

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
  //return teleboy->GetChannelGroupsAmount();
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
    if(bRadio)
        return PVR_ERROR_NO_ERROR;
    //if (teleboy)
    //    return teleboy->GetChannelGroups(handle);

    return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{

    //if (teleboy)
    //    return teleboy->GetChannelGroupMembers(handle, group);

    return PVR_ERROR_SERVER_ERROR;

}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  snprintf(signalStatus.strAdapterName, sizeof(signalStatus.strAdapterName), "Teleboy Adapter 1");
  snprintf(signalStatus.strAdapterStatus, sizeof(signalStatus.strAdapterStatus), "OK");

  return PVR_ERROR_NO_ERROR;
}

const char * GetLiveStreamURL(const PVR_CHANNEL &channel)  {
    return XBMC->UnknownToUTF8(teleboy->GetChannelStreamUrl(channel.iUniqueId).c_str());
}

/** Recording API **/
int GetRecordingsAmount(bool deleted) {
  return 0;
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted) {
  if (deleted) {
    return PVR_ERROR_NO_ERROR;
  }
  if (!teleboy) {
    return PVR_ERROR_SERVER_ERROR;
  }
  teleboy->GetRecordings(handle,"ready");
  return PVR_ERROR_NO_ERROR;
}

int GetTimersAmount(void) {
  return 0;
}

PVR_ERROR GetTimers(ADDON_HANDLE handle) {
  if (!teleboy) {
    return PVR_ERROR_SERVER_ERROR;
  }
  teleboy->GetRecordings(handle,"planned");
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR AddTimer(const PVR_TIMER &timer) {
  if (!teleboy) {
    return PVR_ERROR_SERVER_ERROR;
  }
  if (timer.iEpgUid <= EPG_TAG_INVALID_UID) {
    return PVR_ERROR_REJECTED;
  }
  if (!teleboy->Record(timer.iEpgUid)) {
    return PVR_ERROR_REJECTED;
  }
  PVR->TriggerTimerUpdate();
  PVR->TriggerRecordingUpdate();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording) {
  if (!teleboy) {
    return PVR_ERROR_SERVER_ERROR;
  }
  if (!teleboy->DeleteRecording(recording.strRecordingId)) {
    return PVR_ERROR_REJECTED;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete) {
  if (!teleboy) {
    return PVR_ERROR_SERVER_ERROR;
  }
  if (!teleboy->DeleteRecording(to_string(timer.iClientIndex))) {
    return PVR_ERROR_REJECTED;
  }
  PVR->TriggerTimerUpdate();
  PVR->TriggerRecordingUpdate();
  return PVR_ERROR_NO_ERROR;
}

void addTimerType(PVR_TIMER_TYPE types[], int idx, int attributes) {
  types[idx].iId = idx + 1;
  types[idx].iAttributes = attributes;
  types[idx].iPrioritiesSize = 0;
  types[idx].iLifetimesSize = 0;
  types[idx].iPreventDuplicateEpisodesSize = 0;
  types[idx].iRecordingGroupSize = 0;
  types[idx].iMaxRecordingsSize = 0;
}

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size) {
  addTimerType(types, 0, PVR_TIMER_TYPE_ATTRIBUTE_NONE);
  addTimerType(types, 1, PVR_TIMER_TYPE_IS_MANUAL);
  *size = 2;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR IsRecordable(const EPG_TAG& tag, bool* isRecordable) {

  time_t current_time;
  time(&current_time);

  *isRecordable = true; //tag.endTime > current_time - 60 * 60 * 24 *7;
  return PVR_ERROR_NO_ERROR;
}

bool IsPlayable(const EPG_TAG &tag) {
  if (!teleboy) {
    return false;
  }
  return teleboy->IsPlayable(tag);
  return false;
}

int GetEpgTagUrl(const EPG_TAG &tag, char *url, int urlLen) {
  if (!teleboy) {
    return -1;
  }
  time(&g_pvrTimeShift);
  g_pvrTimeShift -= tag.startTime;
  string strUrl = teleboy->GetEpgTagUrl(tag);
  strncpy(url, strUrl.c_str(), urlLen);
  return urlLen;
}

PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) {
  if (!teleboy) {
    return PVR_ERROR_FAILED;
  }
  //teleboy->SetRecordingPlayCount(recording, count);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) {
  if (!teleboy) {
    return PVR_ERROR_FAILED;
  }
  //teleboy->SetRecordingLastPlayedPosition(recording, lastplayedposition);
  return PVR_ERROR_NO_ERROR;
}

int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) {
  if (!teleboy) {
    return -1;
  }

  //return teleboy->GetRecordingLastPlayedPosition(recording);
  return -1;
}

time_t GetPlayingTime() {
  time_t current_time;
  time(&current_time);
  return current_time - g_pvrTimeShift;
}

/** UNUSED API FUNCTIONS */
bool CanPauseStream(void) { return true; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR MoveChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool OpenRecordedStream(const PVR_RECORDING &recording) { return false; }
void CloseRecordedStream(void) {}
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */) { return 0; }
long long PositionRecordedStream(void) { return -1; }
long long LengthRecordedStream(void) { return 0; }
void DemuxReset(void) {}
void DemuxFlush(void) {}
int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) { return -1; }
long long PositionLiveStream(void) { return -1; }
long long LengthLiveStream(void) { return -1; }
PVR_ERROR RenameRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
PVR_ERROR UpdateTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
unsigned int GetChannelSwitchDelay(void) { return 0; }
bool IsTimeshifting(void) { return false; }
bool IsRealTimeStream(void) { return true; }
void PauseStream(bool bPaused) {}
bool CanSeekStream(void) { return true; }
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {};
time_t GetBufferTimeStart() { return 0; }
time_t GetBufferTimeEnd() { return 0; }
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }

}
