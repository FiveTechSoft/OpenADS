#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace openads::network {
// HRB returns binary string: "OAUTH", u8 version(1), u8 allow(0/1),
// u16LE username length, username, u16LE password length, password.
// Exact framing; no trailing data. Deny MUST carry empty credentials.
struct AuthResult {
    bool allow = false;
    std::string user, password;
    AuthResult() = default;
    AuthResult(const AuthResult&) = delete;
    AuthResult& operator=(const AuthResult&) = delete;
    ~AuthResult() { clear(); }
    void clear() noexcept {
        volatile char* p = password.empty() ? nullptr : &password[0];
        for (std::size_t i=0;i<password.size();++i) p[i]=0;
        password.clear();user.clear();allow=false;
    }
};
inline bool parse_auth_result(std::string_view s, AuthResult& out) {
    out.clear();
    if (s.size()<11 || s.size()>4363 || s.substr(0,5)!="OAUTH" ||
        static_cast<unsigned char>(s[5])!=1 ||
        (s[6]!=0 && s[6]!=1)) return false;
    std::size_t pos=7;
    auto field=[&](std::string_view& v)->bool {
        if(pos+2>s.size())return false;
        std::size_t n=static_cast<std::size_t>(static_cast<unsigned char>(s[pos])) |
            (static_cast<std::size_t>(static_cast<unsigned char>(s[pos+1]))<<8);pos+=2;
        if(n>s.size()-pos)return false;
        v=s.substr(pos,n);pos+=n;return true;
    };
    std::string_view user,pw;
    if(!field(user)||!field(pw)||pos!=s.size()||user.size()>256||pw.size()>4096||
       user.find('\0')!=std::string_view::npos||pw.find('\0')!=std::string_view::npos)
        return false;
    if(s[6]==0)return user.empty()&&pw.empty();
    // A successful hook supplies an explicit identity, never anonymous.
    if(user.empty())return false;
    out.user.assign(user);out.password.assign(pw);out.allow=true;return true;
}
} // namespace openads::network
