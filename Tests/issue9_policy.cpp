#include "../Driver/Issue9Policy.hpp"
#include "../Diagnostics/ReadOnlyPolicy.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    unsigned checks=0;
    for (int darwin=18; darwin<=27; ++darwin) {
        for (int started=0; started<2; ++started) {
            for (int off=0; off<2; ++off) {
                const bool allowed=Issue9Policy::enabled(darwin, started, off);
                if (off || !started || darwin<20 || darwin>24) assert(!allowed);
                else assert(allowed);
                ++checks;
            }
        }
        for (int configured=0; configured<2; ++configured) {
            for (int event=0; event<2; ++event) {
                const bool write=Issue9Policy::mayClearFlag(darwin, configured, event);
                if (!configured || !event || darwin<23 || darwin>24) assert(!write);
                else assert(write);
                ++checks;
            }
        }
    }
    assert(Issue9Policy::quarantined(0x144D,0xA80D)); ++checks;
    assert(!Issue9Policy::quarantined(0x1C5C,0x174A)); ++checks;
    assert(!Issue9Policy::quarantined(0x144D,0xA808)); ++checks;
    assert(!Issue9Policy::quarantined(0xFFFF,0xA80D)); ++checks;
    for (int major=23;major<=25;++major)
    for (int minor=0;minor<=7;++minor)
    for (int revision=0;revision<=1;++revision)
    for (int started=0;started<2;++started)
    for (int optIn=0;optIn<2;++optIn)
    for (int off=0;off<2;++off) {
        const bool allowed=ReadOnlyPolicy::allowed(major,minor,revision,started,optIn,off);
        assert(allowed == (major==24 && minor==6 && revision==0 && started && optIn && !off));
        ++checks;
    }
    for (unsigned rev=0;rev<256;++rev) {
        assert(ReadOnlyPolicy::matches(0x144D,0xA80D,0x01080200|rev)); ++checks;
    }
    assert(!ReadOnlyPolicy::matches(0x144D,0xA80D,0x01080100)); ++checks;
    assert(!ReadOnlyPolicy::matches(0x1C5C,0x174A,0x01080200)); ++checks;
    struct Reader {
        unsigned char bytes[256] {};
        unsigned reads=0;
        unsigned read8(unsigned offset) { assert(offset<256); ++reads; assert(reads<100); return bytes[offset]; }
    };
    Reader normal;
    normal.bytes[0x34]=0x50; normal.bytes[0x50]=5; normal.bytes[0x51]=0xB0; normal.bytes[0xB0]=0x11;
    assert(ReadOnlyPolicy::findCapability(normal,5)==0x50); ++checks;
    assert(ReadOnlyPolicy::findCapability(normal,0x11)==0xB0); ++checks;
    normal.bytes[0xB1]=0x50;
    assert(ReadOnlyPolicy::findCapability(normal,1)==0); ++checks;
    for (unsigned pointer : {0U,0x20U,0x41U,0xFFU}) {
        Reader malformed; malformed.bytes[0x34]=pointer;
        assert(ReadOnlyPolicy::findCapability(malformed,5)==0); ++checks;
    }
    Reader longest;
    longest.bytes[0x34]=0x40;
    for(unsigned off=0x40;off<0xFC;off+=4) longest.bytes[off+1]=off+4;
    assert(ReadOnlyPolicy::findCapability(longest,5)==0); ++checks;
    std::printf("PASS: %u executable policy cases\n",checks);
}
