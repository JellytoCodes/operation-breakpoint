// Exercises the actual pinned OpenFIRE_Perspective.cpp, linked unmodified.
// Synthetic pinhole coordinates; no claim of measured physical working range.
#include <cstdio>
#include <cmath>
#include "OpenFIRE_Perspective.h"

int main() {
    constexpr int rw = 1920*4, rh = 1080*4;
    constexpr double target_x = .625, target_y = .375;
    const double leds[4][2] = {{.3,0}, {.7,0}, {.3,1}, {.7,1}};
    OpenFIRE_Perspective normal, noisy;
    normal.source(512*4,384*4); noisy.source(512*4,384*4);
    double previous_noise = 0;
    std::printf("{\"kind\":\"synthetic native firmware math\",\"cases\":[");
    for(int distance = 1; distance <= 16; distance *= 2) {
        int x[4], y[4];
        for(int i=0;i<4;i++) {
            x[i] = int(std::lround(512+(leds[i][0]-target_x)*640/distance))*4;
            y[i] = int(std::lround(384+(leds[i][1]-target_y)*512/distance))*4;
        }
        normal.warp(x[0],y[0],x[1],y[1],x[2],y[2],x[3],y[3], .3f*rw,0,.7f*rw,0,.3f*rw,rh,.7f*rw,rh);
        noisy.warp(x[0]+4,y[0],x[1]+4,y[1],x[2]+4,y[2],x[3]+4,y[3], .3f*rw,0,.7f*rw,0,.3f*rw,rh,.7f*rw,rh);
        const double u=double(normal.getX())/rw, v=double(normal.getY())/rh;
        const double noise=std::abs(double(noisy.getX()-normal.getX())/rw);
        if(std::abs(u-target_x)>.0004 || std::abs(v-target_y)>.0004) return 1;
        if(distance>1 && noise < previous_noise*1.8) return 2;
        std::printf("%s{\"relative_distance\":%d,\"sensor_width_px\":%.1f,\"sensor_height_px\":%.1f,\"aim_x\":%.8f,\"aim_y\":%.8f,\"one_sensor_px_shift_on_1920_screen_px\":%.4f}",
                    distance==1?"":",",distance,256./distance,512./distance,u,v,noise*1920);
        previous_noise=noise;
    }
    std::printf("],\"physical_distance_accuracy_tested\":false}\n");
    return 0;
}
