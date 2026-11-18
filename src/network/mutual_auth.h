#pragma once
#include <array>
#include <string>
#include <string_view>
namespace openads::network {
inline void mutual_field(std::string& out,std::string_view s){
 for(unsigned i=0;i<4;++i)out.push_back(static_cast<char>(s.size()>>(8*i)));
 out.append(s);
}
inline bool mutual_read(std::string_view in,std::size_t& p,std::string& out,std::size_t max){
 if(p+4>in.size())return false;
 std::size_t n=0;for(unsigned i=0;i<4;++i)n|=std::size_t(static_cast<unsigned char>(in[p++]))<<(8*i);
 if(n>max||n>in.size()-p)return false;
 out.assign(in.substr(p,n));p+=n;return true;
}
inline std::string mutual_transcript(const std::array<std::uint8_t,32>& challenge,const std::array<std::uint8_t,32>& binding,const std::string& selector,const std::string& requested_path){
 std::string s="OpenADS-mutual-v2";mutual_field(s,std::string_view(reinterpret_cast<const char*>(challenge.data()),32));
 mutual_field(s,std::string_view(reinterpret_cast<const char*>(binding.data()),32));mutual_field(s,selector);mutual_field(s,requested_path);return s;
}
inline std::string mutual_begin(const std::string& selector,const std::string& path){std::string s("\0OM2",4);mutual_field(s,selector);mutual_field(s,path);return s;}
}
