#include "OBIRQuadrature.h"
#include <cstdio>
#include <initializer_list>
int main() {
    OBIRQuadrature q;
    for(auto state:{1,0,2,3}) q.edge(state);
    if(q.position!=1) return 1;
    for(auto state:{2,0,1,3}) q.edge(state);
    if(q.position!=0) return 2;
    // Bounce and reverse edges cannot add a detent.
    for(auto state:{1,3,1,3,1,0,2,3}) q.edge(state);
    if(q.position!=1) return 3;
    for(auto state:{0,3}) q.edge(state); // Invalid two-bit transition.
    if(q.position!=1) return 4;
    for(int i=0;i<50;++i) for(auto state:{1,0,2,3}) q.edge(state);
    if(q.position!=51) return 5;
    for(int i=0;i<52;++i) for(auto state:{2,0,1,3}) q.edge(state);
    if(q.position!=0xffffffffu) return 6;
    std::puts("KY040 native decoder: CW/CCW, bounce, invalid edges, wrap PASS");
}
