#pragma once
#include "core.hpp"

namespace vlr {
struct MenuNavigation {
    int page = 3, selected = 0;
    std::string scope;
    void navigate(int destination, std::string output = {});
    bool back();

  private:
    struct Location {
        int page, selected;
        std::string scope;
    };

    std::vector<Location> history;
};

std::wstring colorMenuText(const std::wstring &text, bool selected = false,
                           const std::wstring &selectedStyle = {}, size_t width = 0);
Rgb menuAccent(const Config &config);
bool menuRainbowAccent(const Config &config);
std::wstring brightnessBar(int percent);
int stepBrightness(int percent, int direction);
bool menuReloadAvailable(int page);
int runMenu(bool smokeTest = false);
std::wstring menuSnapshot(const Json &state, int scope = 0, int page = 0);
void launchMenu(bool smokeTest = false);
void launchBackground();
} // namespace vlr
