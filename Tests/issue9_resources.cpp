#include "../Diagnostics/ResourcePolicy.hpp"
#include <cassert>
#include <cstdio>
using namespace ResourceObservation;
struct Entry { bool valid; uint64_t tag,length; };
struct Reader {
    Entry *entries; unsigned size, reads=0;
    unsigned count() { return size; }
    bool descriptor(unsigned i,uint64_t &tag,uint64_t &length) {
        assert(i<size && i<ScanLimit); ++reads;
        tag=entries[i].tag;length=entries[i].length;return entries[i].valid;
    }
};
int main() {
    Reader empty {nullptr,0}; auto a=summarize(empty);
    assert(a.count==0 && a.matches==0 && a.scanned==0 && !a.truncated);
    Entry entries[] {{true,0x83010010,0x4000},{true,0x83010018,0x1000},{false,0x10,0x8000}};
    Reader mixed {entries,3}; auto b=summarize(mixed);
    assert(b.count==3 && b.invalid==1 && b.matches==1 && b.firstLength==0x4000 && b.firstTag==0x83010010);
    Entry duplicates[] {{true,0x10,0x4000},{true,0x83010010,0x2000}};
    Reader duplicate {duplicates,2}; auto c=summarize(duplicate);
    assert(c.matches==2 && c.firstLength==0x4000);
    Entry many[65] {}; many[64]={true,0x10,0x4000};
    Reader bounded {many,65}; auto d=summarize(bounded);
    assert(d.truncated && d.scanned==64 && bounded.reads==64 && d.matches==0);
    assert(barBase(0x8C100004,0)==0x8C100000ULL);
    assert(barBase(0x8C100000,0x1234)==0x8C100000ULL); // high ignored for 32-bit BAR
    assert(barBase(0x8C100004,1)==0x18C100000ULL);
    assert(!usableBarEncoding(0xFFFFFFFF) && !usableBarEncoding(0x8C100001) && !usableBarEncoding(0x8C100002));
    assert(bridgeBase(0x8C10)==0x8C100000ULL && bridgeLimit(0x8C10)==0x8C1FFFFFULL);
    assert(fit(0x8C100004,0,0x4000,true,0x8C10,0x8C10)==1);
    assert(fit(0x8C100004,0,0x100000,true,0x8C10,0x8C10)==1);
    assert(fit(0x8C100004,0,0x100001,true,0x8C10,0x8C10)==2);
    assert(fit(0x8C000004,0,0x4000,true,0x8C10,0x8C10)==2);
    assert(fit(0x8C100004,1,0x4000,true,0x8C10,0x8C10)==2);
    assert(fit(0x8C100004,0,0x4000,false,0x8C10,0x8C10)==0);
    assert(fit(0x8C10000C,0,0x4000,true,0x8C10,0x8C10)==0); // prefetchable requires another window
    assert(fit(0x8C100004,0,0,true,0x8C10,0x8C10)==0);
    assert(fit(4,0,0x4000,true,0,0)==0);
    assert(fit(0x8C100004,0,0x4000,true,0x8C20,0x8C10)==0); // disabled bridge window
    assert(fit(0x8C100004,0,0x4000,true,0x8C11,0x8C10)==0); // reserved bridge bits
    assert(fit(0xFFFFFFF4,0xFFFFFFFF,32,true,0,0xFFF0)==0); // overflow, never wrapped inside
    std::puts("PASS: passive BAR descriptor scan and bridge-window interpretation");
}
