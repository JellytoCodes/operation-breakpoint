// Links the pinned, unmodified firmware perspective implementation.
#include <cstdio>
#include <cmath>
#include "OpenFIRE_Perspective.h"

int main() {
    OpenFIRE_Perspective p;
    p.source(2048, 1536);
    // A previous layout initializes the cached destination transform.
    p.warp(1200, 400, 2800, 400, 1200, 2800, 2800, 2800,
           3840, 0, 0, 2160, 3840, 4320, 7680, 2160);
    // New Square coordinates and target positions, without deinit.
    p.warp(1200, 400, 2800, 400, 1200, 2800, 2800, 2800,
           2000, 0, 5680, 0, 2000, 4320, 5680, 4320);
    const int staleX=p.getX(), staleY=p.getY();
    p.source(2048, 1536);
    p.deinit(false); // Same operation as SelectCalProfile(currentProfile).
    p.warp(1200, 400, 2800, 400, 1200, 2800, 2800, 2800,
           2000, 0, 5680, 0, 2000, 4320, 5680, 4320);
    const int refreshedX=p.getX(), refreshedY=p.getY();
    if(staleX==refreshedX && staleY==refreshedY) return 1;
    if(std::abs(refreshedX-3950)>2 || std::abs(refreshedY-2044)>2) return 2;
    std::printf("{\"kind\":\"synthetic native layout refresh\",\"stale_xy\":[%d,%d],\"refreshed_xy\":[%d,%d],\"cache_invalidated_by_same_profile\":true}\n",
                staleX, staleY, refreshedX, refreshedY);
    return 0;
}
