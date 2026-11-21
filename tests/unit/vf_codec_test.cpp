#include "network/vf_codec.h"
#include "doctest.h"
#include <limits>
TEST_CASE("vf codec rejects malformed packets and preserves signed values") {
 using namespace openads::network::vf;
 Request a; a.op=OADS_VF_SEEK; a.handle=0xffffffff; a.value1=std::numeric_limits<std::int64_t>::min();
 a.value2=std::numeric_limits<std::int64_t>::max(); a.path1="test"; a.data={0,1,0,255};
 std::vector<std::uint8_t> bytes; CHECK(encode(a,bytes)); Request b; CHECK(decode(bytes,b));
 CHECK((b.value1==a.value1 && b.value2==a.value2 && b.handle==a.handle && b.data==a.data));
 for (std::size_t n=0;n<bytes.size();++n) { auto bad=bytes; bad.resize(n); CHECK(!decode(bad,b)); }
 auto bad=bytes; bad.push_back(0); CHECK(!decode(bad,b)); bad=bytes; bad[6]=1; CHECK(!decode(bad,b));
 bad=bytes; bad[32]=255; bad[33]=255; bad[34]=255; bad[35]=255; CHECK(!decode(bad,b));
 bad=bytes; bad[44]=0; CHECK(!decode(bad,b)); a.path1=std::string("a\0b",3); CHECK(!encode(a,bytes));
 Reply r; r.error=25;r.value1=-1;r.value2=0x7fffffffffffffffLL;r.data={0,0,255};
 CHECK(encode(r,bytes));Reply s;CHECK(decode(bytes,s));CHECK((s.error==25&&s.value1==-1&&s.data==r.data));
 bad=bytes;bad.push_back(0);CHECK(!decode(bad,s));
}
