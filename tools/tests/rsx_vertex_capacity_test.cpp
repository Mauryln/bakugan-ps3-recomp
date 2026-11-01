#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#include <cstdlib>
typedef unsigned u32;typedef uint64_t u64;
#include "vertex_capacity.inc"
void check(bool c){if(!c){puts("FAIL: growing VP upload capacity");exit(1);}}
int main(){
 check(vp_capacity_for(0,1)==1);
 check(vp_capacity_for(393216,4194305)==0);
 check(vp_capacity_for(393216,393216)==393216);
 check(vp_capacity_for(393216,393217)>=393217);
 check(vp_capacity_for(393216,1000000)>=1000000);
 std::vector<unsigned char> a(2*4*256),b(2*8*256,0);
 memset(a.data(),0x37,4*256);memset(a.data()+4*256,0x91,4*256);
 vp_copy_vertex_frames(b.data(),8,a.data(),4);
 for(int i=0;i<4*256;i++){check(b[i]==0x37);check(b[8*256+i]==0x91);}
 std::vector<unsigned char> gpu(2*8*256,0xA5);
 vp_flush_vertex_frame(gpu.data(),b.data(),8,1,3*256);
 for(int i=0;i<8*256;i++)check(gpu[i]==0xA5);
 for(int i=0;i<3*256;i++)check(gpu[8*256+i]==0x91);
 for(int i=3*256;i<8*256;i++)check(gpu[8*256+i]==0xA5);
 vp_flush_vertex_frame(gpu.data(),b.data(),8,0,2*256);
 for(int i=0;i<2*256;i++)check(gpu[i]==0x37);
 for(int i=2*256;i<8*256;i++)check(gpu[i]==0xA5);
 puts("PASS: VP capacity grows beyond the alternate arena limit and preserves both frame parities");
}
