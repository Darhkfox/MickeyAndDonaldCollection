// Is there a newer release? The latest release on GitHub (its version tag, from api.github.com's public "latest
// release" page: no account, nothing about the player sent), read in the background. Checked at most once a day
// (Options > Check for Updates); the launcher shows a badge when there's a newer version.
#pragma once
#include <atomic>
#include <string>
#include <thread>

class UpdateCheck {
public:
    static constexpr const char* kApi = "https://api.github.com/repos/Darhkfox/MickeyAndDonaldCollection/releases/latest";
    static std::string page_for(const std::string& tag) {
        return "https://github.com/Darhkfox/MickeyAndDonaldCollection/releases/tag/" + tag;
    }

    ~UpdateCheck();
    void start();                  // (in the background; nothing if one is running)
    bool running() const { return state_ == 1; }
    // Once, when a check has finished: true and the latest release's tag ("v1.3"), or true and "" when it couldn't
    // be read (offline, ...).
    bool poll(std::string* tag);

    // Is the tag ("v1.3", "1.2.1") a later version than `current` ("1.2")?
    static bool is_newer(const std::string& tag, const std::string& current);

private:
    std::thread thread_;
    std::atomic<int> state_{0};    // 0 idle, 1 running, 2 done
    std::string tag_;
};
