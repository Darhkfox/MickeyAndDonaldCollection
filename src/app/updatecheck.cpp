// Is there a newer release? See updatecheck.h.
#include "updatecheck.h"
#include <cctype>
#include <vector>
#include "sys.h"

UpdateCheck::~UpdateCheck() {
    if (thread_.joinable()) thread_.join();
}

void UpdateCheck::start() {
    if (state_ == 1) return;
    if (thread_.joinable()) thread_.join();
    state_ = 1;
    thread_ = std::thread([this]() {
        std::string body, tag;
        if (http_get(kApi, body)) {
            // ("tag_name": "v1.3": the one field needed, read without a JSON library)
            size_t k = body.find("\"tag_name\"");
            if (k != std::string::npos) {
                size_t q1 = body.find('"', body.find(':', k) + 1);
                size_t q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
                if (q2 != std::string::npos && q2 - q1 < 40) tag = body.substr(q1 + 1, q2 - q1 - 1);
            }
        }
        tag_ = tag;
        state_ = 2;
    });
}

bool UpdateCheck::poll(std::string* tag) {
    if (state_ != 2) return false;
    if (thread_.joinable()) thread_.join();
    *tag = tag_;
    state_ = 0;
    return true;
}

bool UpdateCheck::is_newer(const std::string& tag, const std::string& current) {
    auto parts = [](const std::string& s) {
        std::vector<int> v;
        for (size_t i = 0; i < s.size();) {
            if (!std::isdigit((unsigned char)s[i])) { i++; continue; }
            int n = 0;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) n = n * 10 + (s[i++] - '0');
            v.push_back(n);
        }
        return v;
    };
    std::vector<int> a = parts(tag), b = parts(current);
    if (a.empty()) return false;
    while (a.size() < b.size()) a.push_back(0);
    while (b.size() < a.size()) b.push_back(0);
    return a > b;
}
