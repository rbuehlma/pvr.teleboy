#include "Utils.h"
#ifdef TARGET_WINDOWS
#include "windows.h"
#endif

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>

#include "kodi/Filesystem.h"

std::string Utils::GetFilePath(std::string strPath, bool bUserPath)
{
  return (bUserPath ? kodi::addon::GetAddonPath(strPath) : kodi::addon::GetUserPath(strPath));
}

// http://stackoverflow.com/a/17708801
std::string Utils::UrlEncode(const std::string &value)
{
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (std::string::const_iterator i = value.begin(), n = value.end(); i != n;
      ++i)
  {
    std::string::value_type c = (*i);

    // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
    {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << '%' << std::setw(2) << int((unsigned char) c);
  }

  return escaped.str();
}

double Utils::StringToDouble(const std::string &value)
{
  std::istringstream iss(value);
  double result;

  iss >> result;

  return result;
}

int Utils::StringToInt(const std::string &value)
{
  return (int) StringToDouble(value);
}

std::vector<std::string> Utils::SplitString(const std::string &str,
    const char &delim, int maxParts)
{
  typedef std::string::const_iterator iter;
  iter beg = str.begin();
  std::vector < std::string > tokens;

  while (beg != str.end())
  {
    if (maxParts == 1)
    {
      tokens.push_back(std::string(beg, str.end()));
      break;
    }
    maxParts--;
    iter temp = find(beg, str.end(), delim);
    if (beg != str.end())
      tokens.push_back(std::string(beg, temp));
    beg = temp;
    while ((beg != str.end()) && (*beg == delim))
      beg++;
  }

  return tokens;
}

std::string Utils::ReadFile(const std::string path)
{
  kodi::vfs::CFile file;
  if (!file.CURLCreate(path) || !file.CURLOpen(0))
  {
    kodi::Log(ADDON_LOG_ERROR, "Failed to open file [%s].", path.c_str());
    return "";
  }

  char buf[1025];
  size_t nbRead;
  std::string content;
  while ((nbRead = file.Read(buf, 1024)) > 0)
  {
    buf[nbRead] = 0;
    content.append(buf);
  }
  return content;

}

time_t Utils::StringToTime(std::string timeString)
{
  struct tm tm;

  int year, month, day, h, m, s, tzh, tzm;
  if (sscanf(timeString.c_str(), "%d-%d-%dT%d:%d:%d%d", &year, &month, &day, &h,
      &m, &s, &tzh) < 7)
  {
    tzh = 0;
  }
  tzm = tzh % 100;
  tzh = tzh / 100;

  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = h - tzh;
  tm.tm_min = m - tzm;
  tm.tm_sec = s;

  time_t ret = timegm(&tm);
  return ret;
}
