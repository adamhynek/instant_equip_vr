#include <filesystem>
#include <set>
#include <sstream>

#include "skse64_common/Utilities.h"

#include "config.h"


namespace Config {
    // Define extern options
    Options options;

    bool ReadFloat(const std::string &name, float &val)
    {
        if (!GetConfigOptionFloat("Settings", name.c_str(), &val)) {
            _WARNING("Failed to read float config option: %s", name.c_str());
            return false;
        }

        return true;
    }

    bool ReadDouble(const std::string &name, double &val)
    {
        if (!GetConfigOptionDouble("Settings", name.c_str(), &val)) {
            _WARNING("Failed to read double config option: %s", name.c_str());
            return false;
        }

        return true;
    }

    bool ReadBool(const std::string &name, bool &val)
    {
        if (!GetConfigOptionBool("Settings", name.c_str(), &val)) {
            _WARNING("Failed to read bool config option: %s", name.c_str());
            return false;
        }

        return true;
    }

    bool ReadInt(const std::string &name, int &val)
    {
        if (!GetConfigOptionInt("Settings", name.c_str(), &val)) {
            _WARNING("Failed to read int config option: %s", name.c_str());
            return false;
        }

        return true;
    }

    bool ReadString(const std::string &name, std::string &val)
    {
        std::string	data = GetConfigOption("Settings", name.c_str());
        if (data.empty()) {
            _WARNING("Failed to read str config option: %s", name.c_str());
            return false;
        }

        val = data;
        return true;
    }

    inline void ltrim(std::string &s) { s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !::isspace(ch); })); }
    inline void rtrim(std::string &s) { s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !::isspace(ch); }).base(), s.end()); }
    inline void trim(std::string &s) { ltrim(s); rtrim(s); }

    std::set<std::string, std::less<>> SplitStringToSet(const std::string &s, char delim)
    {
        std::set<std::string, std::less<>> result;
        std::stringstream ss(s);
        std::string item;

        while (getline(ss, item, delim)) {
            trim(item);
            result.insert(item);
        }

        return result;
    }

    bool ReadFormArray(const std::string &name, std::vector<UInt32> &val)
    {
        std::string	data = GetConfigOption("Settings", name.c_str());
        if (data.empty()) {
            _WARNING("Failed to read FormArray config option: %s", name.c_str());
            return false;
        }

        val.clear(); // first empty the set, since we will be reading into it

        std::set<std::string, std::less<>> stringSet = SplitStringToSet(data, ',');
        for (const std::string &str : stringSet) {
            val.push_back(std::stoul(str, nullptr, 16));
        }
        return true;
    }

    bool ReadConfigOptions()
    {
        if (!ReadFloat("skipAnimationDeltaTime", options.skipAnimationDeltaTime)) return false;
        if (!ReadInt("numSkipAnimationFrames", options.numSkipAnimationFrames)) return false;

        if (!ReadFormArray("drawIgnoreFormIDs", options.drawIgnoreFormIDs)) return false;
        if (!ReadFormArray("sheatheIgnoreFormIDs", options.sheatheIgnoreFormIDs)) return false;

        return true;
    }

    const std::string & GetConfigPath()
    {
        static std::string s_configPath;

        if (s_configPath.empty()) {
            std::string	runtimePath = GetRuntimeDirectory();
            if (!runtimePath.empty()) {
                s_configPath = runtimePath + "Data\\SKSE\\Plugins\\instant_equip_vr.ini";

                _MESSAGE("config path = %s", s_configPath.c_str());
            }
        }

        return s_configPath;
    }

    std::string GetConfigOption(const char *section, const char *key)
    {
        std::string	result;

        const std::string & configPath = GetConfigPath();
        if (!configPath.empty()) {
            char	resultBuf[256];
            resultBuf[0] = 0;

            UInt32	resultLen = GetPrivateProfileString(section, key, NULL, resultBuf, sizeof(resultBuf), configPath.c_str());

            result = resultBuf;
        }

        return result;
    }

    bool GetConfigOptionDouble(const char *section, const char *key, double *out)
    {
        std::string	data = GetConfigOption(section, key);
        if (data.empty())
            return false;

        *out = std::stod(data);
        return true;
    }

    bool GetConfigOptionFloat(const char *section, const char *key, float *out)
    {
        std::string	data = GetConfigOption(section, key);
        if (data.empty())
            return false;

        *out = std::stof(data);
        return true;
    }

    bool GetConfigOptionInt(const char *section, const char *key, int *out)
    {
        std::string	data = GetConfigOption(section, key);
        if (data.empty())
            return false;

        *out = std::stoi(data);
        return true;
    }

    bool GetConfigOptionBool(const char *section, const char *key, bool *out)
    {
        std::string	data = GetConfigOption(section, key);
        if (data.empty())
            return false;

        int val = std::stoi(data);
        if (val == 1) {
            *out = true;
            return true;
        }
        else if (val == 0) {
            *out = false;
            return true;
        }
        else {
            return false;
        }
    }
}
