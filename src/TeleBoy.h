//
// Created by johannes on 04.02.16.
//

#include "client.h"
#include "JsonParser.h"
#include "Curl.h"
#include "UpdateThread.h"
#include <map>

/*!
 * @brief PVR macros for string exchange
 */
#define PVR_STRCPY(dest, source) do { strncpy(dest, source, sizeof(dest)-1); dest[sizeof(dest)-1] = '\0'; } while(0)
#define PVR_STRCLR(dest) memset(dest, 0, sizeof(dest))

#define JS_STR(G, STR) do {                                             \
        string _s = (STR);                                         \
        yajl_gen_string(G, (const unsigned char*)_s.c_str(), _s.length());      \
} while (0)

struct TeleBoyChannel
{
    int         id;
    std::string name;
    std::string logoPath;
};

struct PVRIptvEpgGenre
{
    int               iGenreType;
    int               iGenreSubType;
    std::string       strGenre;
};



class TeleBoy
{
public:
    TeleBoy(bool favoritesOnly);
    virtual ~TeleBoy();
    virtual bool Login(string u, string p);
    virtual bool LoadChannels();
    virtual void GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities);
    virtual int GetChannelsAmount(void);
    virtual PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio);
    virtual void TransferChannel(ADDON_HANDLE handle, TeleBoyChannel channel, int channelNum);
    virtual std::string GetChannelStreamUrl(int uniqueId);
    virtual PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd);
    virtual bool Record(int programId);
    virtual bool DeleteRecording(string recordingId);
    virtual void GetRecordings(ADDON_HANDLE handle, string type);
    virtual bool IsPlayable(const EPG_TAG *tag);
    virtual bool IsRecordable(const EPG_TAG *tag);
    virtual string GetEpgTagUrl(const EPG_TAG *tag);
    virtual string GetRecordingStreamUrl(string recordingId);


protected:
    virtual string HttpGet(string url);
    virtual string HttpPost(string url, string postData);
    virtual void ApiSetHeader();
    virtual yajl_val ApiGetResult(string content);
    virtual yajl_val ApiGet(string url);
    virtual yajl_val ApiPost(string url, string postData);
    virtual yajl_val ApiDelete(string url);

private:
    virtual string formatDateTime(time_t dateTime);
    string username;
    string password;
    bool favoritesOnly;
    string userId;
    map<int, TeleBoyChannel> channelsById;
    vector<int> sortedChannels;
    Curl *curl;
    int64_t maxRecallSeconds;
    UpdateThread *updateThread;
};
