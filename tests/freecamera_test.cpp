#include "freecamera_math.h"
#include "freecamera_fruit_layout.h"
#include "freecamera_visibility.h"
#include "freecamera_fog.h"
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    using namespace nx::freecamera;
    float normal[3];
    for (float angle:{0.f,pi/2,pi,3*pi/2}) {
        const float plane[]{std::sin(angle),.5f,-std::cos(angle),-.25f};
        check(view_fog_normal(plane,.8f,.01f,normal),"Directional fog plane must be adapted");
        check(normal[0]==0 && normal[1]==0 && std::fabs(normal[2]+std::sqrt(1.25f))<1e-5f,
              "Equal view depth must produce the same fog attenuation at every heading");
    }
    const float fog_plane[]{1,0,0,-.25f};
    check(!view_fog_normal(fog_plane,0,.01f,normal) && !view_fog_normal(fog_plane,1,0,normal),
          "Disabled fog and degenerate falloff must remain native");
    check(camera_space(0x7a88a4)==CameraSpace::world && camera_space(0x7a8830)==CameraSpace::drum,
          "Both field/drum matrix branches must receive the orbit in their own coordinate spaces");
    check(camera_space(0x376d224)==CameraSpace::other,"Minimap camera must remain outside orbit scope");
    check(outdoor_projection(2.557646f,100000,.541052f),"The live camera303 31-degree field projection must be recognized");
    check(!outdoor_projection(7605.31f,9525.31f,.087266f) && !outdoor_projection(1,10000,.698132f),
          "Minimap and UI projections must not set outdoor visibility");
    const float ground_eye[]{0,100,100}, ground_target[]{0,0,0};
    Bounds footprint;
    check(view_bounds(ground_eye,ground_target,.2f,1,0,0,300,0,0,0,footprint),"Ground footprint must be valid");
    check(std::fabs(footprint.left-35.35534f)<.01f && std::fabs(footprint.far-50)<.01f &&
          std::fabs(footprint.near-33.33333f)<.01f,"Perspective footprint must match analytic ground intersections");
    const float rear_eye[]{0,100,-100};Bounds rear;
    check(view_bounds(rear_eye,ground_target,.2f,1,0,0,300,0,0,0,rear) &&
          std::fabs(rear.near-footprint.far)<.01f && std::fabs(rear.far-footprint.near)<.01f,
          "Looking backward must preserve forward visibility distance");
    const float zoom_eye[]{0,200,200};Bounds zoom;
    check(view_bounds(zoom_eye,ground_target,.2f,1,0,0,300,0,0,0,zoom) && zoom.left>footprint.left*1.9f,
          "Zooming out must expand the visible ground footprint");
    Bounds wide;
    check(view_bounds(ground_eye,ground_target,.2f,2,0,0,300,0,0,0,wide) && wide.left>footprint.left*1.9f,
          "Wider aspect must load both newly visible edges");
    // Independently project ground/canopy sample points into camera space at
    // multiple yaws, elevations and zooms; every on-screen sample inside the
    // configured distance budget must be inside the broad-phase rectangle.
    for (int yaw=0;yaw<360;yaw+=30) for (float elevation:{.1f,.6f,1.4f}) for (float radius:{60.f,180.f,360.f}) {
        const float a=yaw*pi/180, h=radius*std::cos(elevation);
        const float eye[]{h*std::sin(a),radius*std::sin(elevation),h*std::cos(a)};
        Bounds bound;
        check(view_bounds(eye,ground_target,.2f,1.8f,0,0,300,-48,72,20,bound),"Swept footprint must be valid");
        for (int x=-300;x<=300;x+=20) for(int z=-300;z<=300;z+=20) for(float y:{-48.f,0.f,72.f}) {
            const float dx=x-eye[0],dy=y-eye[1],dz=z-eye[2];
            const float depth=-(dx*eye[0]+dy*eye[1]+dz*eye[2])/radius;
            const float sx=std::cos(a)*dx-std::sin(a)*dz;
            const float sy=-std::sin(elevation)*(std::sin(a)*dx+std::cos(a)*dz)+std::cos(elevation)*dy;
            if (depth>=1 && depth<=radius+300 && std::fabs(sx)<=depth*.36f && std::fabs(sy)<=depth*.2f)
                check(x>=-bound.left-.01f && x<=bound.right+.01f && z>=-bound.far-.01f && z<=bound.near+.01f,
                      "Visibility must contain every projected sample across yaw, pitch, zoom and height");
        }
    }
    check(!view_bounds(ground_eye,ground_target,0,1,0,0,300,0,0,0,footprint),"Invalid projection must use fallback");
    const std::string fruit_config="ACNH_TREE_FRUIT_V1 oak -6 16.8 9.4 6.2 16 9.6 0 24.5 5.6 sakura -6 16.8 9.4 6.2 16 9.6 0 24.5 5.6 snow -9 16.8 9 10 16.5 8 -2 24.5 6 palm -2.5 15.5 3.2 2.6 16.3 2.3";
    FruitLayouts fruit;
    std::istringstream valid_fruit(fruit_config);
    check(read_fruit_layouts(valid_fruit,fruit),"Valid fruit layout must load");
    for (const auto& bad : {std::string(""),fruit_config+" extra",fruit_config.substr(0,40),std::string("ACNH_TREE_FRUIT_V1 oak 1000")}) {
        std::istringstream input(bad);FruitLayouts unchanged=fruit;
        check(!read_fruit_layouts(input,fruit),"Malformed fruit profiles must fail closed");
        check(fruit.oak==unchanged.oak,"Failed profile parse must not partially update origins");
    }
    check(fruit_variant("rom:/Model/PltTreeOakSnow.Nin_NX_NVN.zs")==2,"Snow model selects snow attachments");
    check(fruit_variant("rom:/Model/PltTreeOakAnim.Nin_NX_NVN.zs")==-1,"Animation loads must not reset seasonal attachments");
    check(fruit_variant("rom:/Model/IdrObjTreeOak00.Nin_NX_NVN.zs")==-1,"Museum models must not reset island attachments");
    Controls controls;
    Orbit controlled;
    controls.update(controlled,true,false,false,false,1,.5f,.02f);
    check(controlled.yaw>0 && controlled.pitch>0,"Outdoor camera must orbit immediately without a toggle");
    controls.update(controlled,true,false,false,true,0,1,.02f);
    const Orbit zoomed=controlled;
    check(zoomed.zoom<1,"Holding R3 and pushing up must zoom in");
    controls.update(controlled,true,false,false,false,0,0,.02f);
    check(controlled.zoom==zoomed.zoom && controlled.yaw==zoomed.yaw,"Releasing a zoom gesture must preserve its result");
    controls.update(controlled,true,false,false,true,0,0,.02f);
    controls.update(controlled,true,false,false,false,0,0,.02f);
    check(controlled.identity(),"An R3 click must recenter");
    controls.update(controlled,true,false,true,false,0,-1,.02f);
    check(controlled.zoom>1 && controlled.pitch==0,"F6 is a held zoom modifier, not a mode toggle");
    const Orbit suspended=controlled;
    controls.update(controlled,false,true,false,true,1,1,.02f);
    controls.update(controlled,true,false,false,false,0,0,.02f);
    check(controlled.zoom==suspended.zoom && controlled.yaw==suspended.yaw,"Inactive cameras must ignore input and cancel pending clicks");
    controls.update(controlled,true,true,false,false,1,1,.02f);
    check(controlled.identity(),"F7 must reset without simultaneously orbiting");
    const Bounds original_bounds{140, 140, 60, 265};
    const Bounds quarter = rotate_bounds(original_bounds, pi/2);
    check(std::fabs(quarter.left-265)<.001f && std::fabs(quarter.right-60)<.001f &&
          std::fabs(quarter.near-140)<.001f && std::fabs(quarter.far-140)<.001f,
          "Quarter-turn visibility must transfer forward distance to the world X axis");
    const Bounds half = rotate_bounds(original_bounds, pi);
    check(std::fabs(half.near-265)<.001f && std::fabs(half.far-60)<.001f,
          "Half-turn visibility must look behind the original camera");
    float mx, my;
    movement(pi/2, 0, 1, mx, my);
    check(std::fabs(mx+1)<.0001f && std::fabs(my)<.0001f, "Camera-relative up at 90 degrees must move world-left");
    movement(pi, 1, 0, mx, my);
    check(std::fabs(mx+1)<.0001f && std::fabs(my)<.0001f, "Camera-relative right at 180 degrees must move world-left");
    movement(0, .3f, .7f, mx, my);
    check(std::fabs(mx-.3f)<.0001f && std::fabs(my-.7f)<.0001f, "Stock camera must preserve movement");
    float pos[] = {0, 50, 100}, at[] = {0, 0, 0}, out[3];
    Orbit o;
    o.yaw = pi;
    check(o.apply(pos, at, out) && std::fabs(out[0]) < .001f && std::fabs(out[2]+100) < .001f,
          "Half orbit must reveal the opposite side of the target");
    o.yaw = 2*pi;
    check(o.apply(pos, at, out) && std::fabs(out[2]-100) < .001f, "Full orbit must return to start");
    o.zoom = .5f;
    check(o.apply(pos, at, out) && std::fabs(out[1]-25) < .001f && std::fabs(out[2]-50) < .001f,
          "Zoom must scale distance without changing target");
    o = {};
    bool left = false, right = false, behind = false;
    for (int i = 0; i < 1000; i++) {
        o.advance(1, 0, 0, .01f);
        check(o.apply(pos, at, out), "Continuous orbit must remain valid across wraps");
        left |= out[0] < -90; right |= out[0] > 90; behind |= out[2] < -90;
    }
    check(left && right && behind, "Orbit must cross all four quadrants");
    const float moving_at[] = {123, 10, -456}, moving_pos[] = {143, 70, -356};
    for (int i = 0; i <= 360; i++) {
        o = {}; o.yaw = i*pi/180;
        check(o.apply(moving_pos, moving_at, out), "Offset orbit must remain valid");
        const float dx = out[0]-moving_at[0], dz = out[2]-moving_at[2];
        check(std::fabs(dx*dx+dz*dz-10400) < .1f && std::fabs(out[1]-70) < .001f,
              "Yaw must preserve horizontal radius and height at every angle");
    }
    for (int i = 0; i < 500; i++) o.advance(0, 1, -1, .05f);
    check(o.zoom == .25f && o.apply(pos, at, out) && std::isfinite(out[2]), "Pitch and zoom limits must remain finite");
    float invalid[] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    check(!o.apply(invalid, at, out) && !o.apply(at, at, out), "Reject invalid and degenerate views");
    o = {};
    check(o.identity(), "Reset must restore the unmodified camera");
    std::puts("freecamera: orbit, wrap, zoom, limits, invalid input and reset passed");
}
