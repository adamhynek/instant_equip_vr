#pragma once

namespace Config {
    struct Options {
        int numSkipAnimationFrames = 1;
        float skipAnimationDeltaTime = 10000.f;
        std::vector<UInt32> drawIgnoreFormIDs = {};
        std::vector<UInt32> sheatheIgnoreFormIDs = {};
    };
    extern Options options; // global object containing options


    // Fills Options struct from INI file
    bool ReadConfigOptions();

    const std::string & GetConfigPath();

    std::string GetConfigOption(const char * section, const char * key);

    bool GetConfigOptionDouble(const char *section, const char *key, double *out);
    bool GetConfigOptionFloat(const char *section, const char *key, float *out);
    bool GetConfigOptionInt(const char *section, const char *key, int *out);
    bool GetConfigOptionBool(const char *section, const char *key, bool *out);
}
