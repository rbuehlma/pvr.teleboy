#include "UpdateThread.h"
#include "categories.h"
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include "sql/ParameterDB.h"
#include "http/HttpClient.h"
#include "Session.h"

#include "kodi/addon-instance/PVR.h"

using namespace std;

struct TeleBoyChannel
{
  int id;
  std::string name;
  std::string logoPath;
};

struct TeleboyGenre
{
  std::string name;
  std::string nameEn;
};

class ATTR_DLL_LOCAL TeleBoy : public kodi::addon::CAddonBase,
                               public kodi::addon::CInstancePVRClient
{
public:
  TeleBoy();
  ~TeleBoy() override;

  ADDON_STATUS Create() override;
  ADDON_STATUS SetSetting(const std::string& settingName,
                          const kodi::addon::CSettingValue& settingValue) override;

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;

  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;
  PVR_ERROR GetChannelStreamProperties(const kodi::addon::PVRChannel& channel,
        PVR_SOURCE source,
        std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end,
        kodi::addon::PVREPGTagsResultSet& results) override;
  void GetEPGForChannelAsync(int uniqueChannelId, time_t iStart, time_t iEnd);
  void GetEPGForBroadcastAsync(unsigned int uniqueBroadcastId);
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount) override;
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results) override;
  PVR_ERROR GetRecordingStreamProperties(const kodi::addon::PVRRecording& recording,
        std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording) override;
  PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recording,
        std::vector<kodi::addon::PVREDLEntry>& edl) override;
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types) override;
  PVR_ERROR GetTimersAmount(int& amount) override;
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& isPlayable) override;
  PVR_ERROR IsEPGTagRecordable(const kodi::addon::PVREPGTag& tag, bool& isRecordable) override;
  PVR_ERROR GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag,
        std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR GetEPGTagEdl(const kodi::addon::PVREPGTag& tag,
        std::vector<kodi::addon::PVREDLEntry>& edl) override;
  void UpdateConnectionState(const std::string& connectionString, PVR_CONNECTION_STATE newState, const std::string& message);
  bool SessionInitialized();

private:
  map<int, TeleBoyChannel> channelsById;
  map<int, TeleboyGenre> genresById;
  static std::mutex sendEpgToKodiMutex;
  vector<int> sortedChannels;
  vector<UpdateThread*> updateThreads;
  Categories m_categories;
  ParameterDB *m_parameterDB;
  HttpClient *m_httpClient;
  Session *m_session;

  virtual string FormatDate(time_t dateTime);
  virtual bool ApiGetResult(string content, nlohmann::json& doc);
  virtual bool ApiGet(string url, nlohmann::json& doc, time_t cacheDuration);
  virtual bool ApiGetWithoutConnectedCheck(string url, nlohmann::json& doc, time_t timeout);
  virtual bool ApiPost(string url, string postData, nlohmann::json& doc);
  virtual bool ApiDelete(string url, nlohmann::json& doc);
  virtual string FollowRedirect(string url);
  virtual string GetStringOrEmpty(const nlohmann::json& jsonValue, const char* fieldName);
  void TransferChannel(kodi::addon::PVRChannelsResultSet& results, TeleBoyChannel channel,
      int channelNum);
  bool WriteDataJson();
  bool ReadDataJson();
  std::string GetStreamParameters();
  void LoadGenres();
  bool LoadChannels();
  PVR_ERROR SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
        const nlohmann::json& stream, bool realtime);
  void AddTimerType(std::vector<kodi::addon::PVRTimerType>& types, int idx, int attributes);
  PVR_ERROR FetchJsonForStream(const std::string& streamId, nlohmann::json& stream_json);
  void AddCommercialBreaks(const std::string& streamId, std::vector<kodi::addon::PVREDLEntry>& edl);
  void UpdateEPGFromJson(const nlohmann::json& item, bool isDetailedEPGData);
};
