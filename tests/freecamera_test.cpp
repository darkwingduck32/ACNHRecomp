#include "features/camera/freecamera_math.h"
#include "features/camera/preferences.h"
#include "features/camera/freecamera_fruit_layout.h"
#include "features/camera/freecamera_visibility.h"
#include "features/camera/freecamera_fog.h"
#include "features/camera/freecamera_sky.h"
#include "features/camera/freecamera_ao_math.h"
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    using namespace nx::freecamera;
    check(!sky_correction_required(false,true,60,100), "Rooms and UI must retain native sky behavior");
    check(!sky_correction_required(true,false,31,0), "Unmodified camera with orbit disabled retains native sky");
    check(sky_correction_required(true,true,31,0), "Orbit requires sky correction");
    check(sky_correction_required(true,false,60,0), "Custom FOV requires sky correction with orbit disabled");
    check(sky_correction_required(true,false,31,100) && sky_correction_required(true,false,31,-100),
          "Both zoom directions require sky correction with orbit disabled");
    for (auto resource : {"Model/PltTreeOak.Nin_NX_NVN.zs", "Model/CloudVR.Nin_NX_NVN.zs",
                          "Model/FldOutS00.Nin_NX_NVN.zs", "System/Resource/ResourceSizeTable.srsizetable"}) {
        check(!resource_override_allowed(true, false, resource), "Original-model mode must bypass camera models and their size table");
        check(resource_override_allowed(true, true, resource), "Replacement-model mode must retain camera resources");
        check(resource_override_allowed(false, false, resource), "Camera model choices must not filter other mods");
    }
    check(resource_override_allowed(true, false, "Pack/StaticParam.pack") &&
          resource_override_allowed(false, false, "Pack/StaticParam.pack"),
          "Model replacement must not remove camera parameters or native SpeedFix data");
    {
        const auto custom = preferences(60, 175, 200, 50, true);
        check(std::fabs(custom.fov - pi / 3) < .000001f && custom.reset_zoom == 1.75f,
              "Configured FOV/zoom must use degrees and percent");
        const auto bounded = preferences(999, -99, 999, -99, false);
        check(std::fabs(bounded.fov - 80 * pi / 180) < .000001f && bounded.reset_zoom == .25f &&
                  bounded.orbit_speed == 3 && bounded.zoom_speed == .25f,
              "Configuration outside UI ranges must be bounded");
        Orbit orbit{1, .4f, 2};
        Controls controls;
        controls.update(orbit, true, true, false, false, 0, 0, .02f, custom.reset_zoom);
        check(orbit.yaw == 0 && orbit.pitch == 0 && orbit.zoom == 1.75f,
              "Keyboard reset must use the configured distance and recenter");
        controls.update(orbit, true, false, false, true, 0, 0, .02f, custom.reset_zoom);
        controls.update(orbit, true, false, false, false, 0, 0, .02f, custom.reset_zoom);
        check(orbit.zoom == 1.75f, "R3 click reset must use the same configured distance");
        Orbit normal, fast, inverted;
        Controls a, b, c;
        a.update(normal, true, false, false, false, 1, 1, .02f);
        b.update(fast, true, false, false, false, 1, 1, .02f, 1, 2, 1);
        c.update(inverted, true, false, false, false, 1, 1, .02f, 1, 1, 1, true);
        check(std::fabs(fast.yaw - 2 * normal.yaw) < .000001f && std::fabs(fast.pitch - 2 * normal.pitch) < .000001f,
              "Orbit sensitivity must scale both axes");
        check(inverted.pitch == -normal.pitch && inverted.yaw == normal.yaw,
              "Vertical inversion must not invert horizontal orbit");
        Orbit zoom, zoom_inverted;
        Controls d, e;
        d.update(zoom, true, false, true, false, 0, 1, .02f, 1, 1, .5f);
        e.update(zoom_inverted, true, false, true, false, 0, 1, .02f, 1, 1, .5f, true);
        check(zoom.zoom == zoom_inverted.zoom && zoom.zoom < 1,
              "Vertical orbit inversion must not change zoom direction");
        auto before = orbit;
        controls.update(orbit, false, true, false, false, 1, 1, .02f, 1);
        check(orbit.zoom == before.zoom && orbit.yaw == before.yaw,
              "UI/interior gating must suppress resets and input");
    }
    {
        const float direction[]{-.0597f,.3177f,-.9463f};
        for (float elevation : {.05f,.15f,.3f,.53f,.707f}) {
            const float ray[]{0,-elevation,-std::sqrt(1-elevation*elevation)};
            const float scale=directional_glare_scale(ray,direction,12);
            const float dot=ray[1]*direction[1]+ray[2]*direction[2];
            const float reference=-.70710678f*direction[1]-std::sqrt(.5f)*direction[2];
            check(scale>0 && scale<=1 && std::fabs(scale*std::pow(dot,12)-std::pow(reference,12))<.000001f,
                  "Directional glint must retain the reference response at shallow angles");
            for (float yaw : {pi/2,pi,3*pi/2}) {
                const float c=std::cos(yaw),s=std::sin(yaw);
                const float rotated_ray[]{s*ray[2],ray[1],c*ray[2]};
                const float rotated_light[]{c*direction[0]+s*direction[2],direction[1],-s*direction[0]+c*direction[2]};
                check(std::fabs(directional_glare_scale(rotated_ray,rotated_light,12)-scale)<.00001f,
                      "Glint correction must depend on light-relative heading, not world direction");
            }
        }
        for (float elevation : {.70710678f,.8f,1.f}) {
            const float ray[]{0,-elevation,-std::sqrt(1-elevation*elevation)};
            check(directional_glare_scale(ray,direction,12)==1,"Normal camera glints must remain unchanged");
        }
        const float ray[]{0,0,-1}, away[]{0,0,1};
        check(directional_glare_scale(ray,away,12)==1,"Views away from the glint must remain unchanged");
        check(directional_glare_scale(ray,direction,0)==1 &&
              directional_glare_scale(ray,direction,std::numeric_limits<float>::quiet_NaN())==1,
              "Invalid glint powers must leave lighting unchanged");
        const float bad_ray[]{0,0,-2}, bad_light[]{0,std::numeric_limits<float>::infinity(),-1};
        check(directional_glare_scale(bad_ray,direction,12)==1 &&
              directional_glare_scale(ray,bad_light,12)==1,
              "Invalid light and view directions must leave lighting unchanged");
    }
    {
        const float projection[]{.01f,0,0,0, 0,.01f,0,0, 0,0,-.01f,0, 0,0,0,1};
        for(float yaw:{0.f,pi/4,pi/2,pi,3*pi/2}) {
            const float c=std::cos(yaw),s=std::sin(yaw);
            const float view[]{c,0,s,-10*c-20*s, -s,0,c,10*s-20*c, 0,1,0,-100};
            AoClip clip;
            check(clip.set(view,projection),"AO must accept rotated top-down orthographic maps");
            check(!clip.outside({{10,100000,20},5},0),"AO must not reject on untested depth planes");
            check(!clip.outside({{10+110*c,0,20+110*s},10},0),"AO must retain spheres touching a map edge");
            check(clip.outside({{10+112*c,0,20+112*s},10},0),"AO must reject spheres wholly outside a map edge");
            check(!clip.outside({{10,0,20},-1},0),"AO invalid bounds must fail open");
            check(clip.outside({{10,0,20},5},500),"AO must apply the native drum translation");
            float invalid_projection[16];
            std::copy_n(projection,16,invalid_projection);
            invalid_projection[14]=-1;
            check(!clip.set(view,invalid_projection) && !clip.outside({{1000,0,1000},1},0),
                  "Invalid AO projection must clear previously valid clipping state");
        }
        const float local[]{2,3,4,1}, transform[]{2,1,0,10, 0,3,.5f,2, 0,0,4,20};
        const AoSphere bound=ao_transform_sphere(local,transform,20);
        check(bound.p[0]==17 && bound.p[1]==13 && bound.p[2]==36,"AO must transform model-local sphere centers");
        for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) for(int z=-1;z<=1;++z) {
            const float n=std::sqrt(float(x*x+y*y+z*z));
            if(!n) continue;
            const float dx=(2*x+y)/n,dy=(3*y+.5f*z)/n,dz=4*z/n;
            check(std::sqrt(dx*dx+dy*dy+dz*dz)+20<=bound.radius+.0001f,
                  "AO bounds must contain scaled/sheared geometry plus deformation margin");
        }
        float bad[]{2,3,4,-1};
        check(ao_transform_sphere(bad,transform,20).radius<0,"Negative native sphere radius must fail open");
        bad[3]=std::numeric_limits<float>::infinity();
        check(ao_transform_sphere(bad,transform,20).radius<0,"Nonfinite AO bounds must fail open");
    }
    for (float yaw : {0.f, pi/2, pi, 3*pi/2}) {
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float expected[]{381.887f, 59.4954f, 5.80881f};
        const float view[]{c,0,s,-(c*expected[0]+s*expected[2]),
                           0,1,0,-expected[1],
                          -s,0,c,-(-s*expected[0]+c*expected[2])};
        float eye[3];
        check(sky_camera_center(view,eye), "Rigid sky view must recover the camera eye");
        for (int k=0;k<3;++k)
            check(std::fabs(eye[k]-expected[k])<.001f, "Sky center must follow the eye at every heading");
        for (int row=0;row<3;++row)
            check(std::fabs(view[row*4]*eye[0]+view[row*4+1]*eye[1]+view[row*4+2]*eye[2]+view[row*4+3])<.001f,
                  "Sky center must transform to the view origin");
    }
    for (float yaw:{0.f,pi/2,pi,3*pi/2}) for (float scale:{.3f,1.f}) {
        const float c=std::cos(yaw),sn=std::sin(yaw),ct=std::cos(.1276f),st=std::sin(.1276f);
        float cloud[]{scale*c,-scale*sn*st,scale*sn*ct,100,
                      0,scale*ct,scale*st,20,
                      -scale*sn,-scale*c*st,scale*c*ct,200};
        const float front_height=20-525.18f*scale*st;
        check(cloud_horizon_model(cloud),"Native cloud tilt must become a level depressed horizon");
        for (int degree=0;degree<360;degree+=15) {
            const float x=525.18f*std::sin(degree*pi/180),z=525.18f*std::cos(degree*pi/180);
            check(std::fabs(cloud[4]*x+cloud[6]*z+cloud[7]-front_height)<.001f,
                  "Cloud equator must preserve the native front depression at every heading");
        }
        check(std::fabs(cloud[0]-scale*c)<.001f && std::fabs(cloud[8]+scale*sn)<.001f,
              "Leveling the cloud horizon must retain heading and scale");
    }
    float invalid_cloud[]{1,0,0,0, 0,2,0,0, 0,0,1,0};
    check(!cloud_horizon_model(invalid_cloud) && invalid_cloud[5]==2,
          "Unexpected cloud scales must remain unchanged");
    const float invalid_sky_view[12]{};
    float unused_eye[3];
    check(!sky_camera_center(invalid_sky_view,unused_eye), "Invalid sky view must retain native placement");
    float normal[4];
    for (float angle:{0.f,pi/2,pi,3*pi/2}) {
        const float plane[]{std::sin(angle),.5f,-std::cos(angle),-.25f};
        check(view_fog_plane(plane,.8f,.01f,0,normal),"Directional fog plane must be adapted");
        check(normal[0]==0 && normal[1]==0 && std::fabs(normal[2]+std::sqrt(1.25f))<1e-5f,
              "Equal view depth must produce the same fog attenuation at every heading");
    }
    const float fog_plane[]{1,0,0,-.25f};
    check(!view_fog_plane(fog_plane,0,.01f,0,normal) && !view_fog_plane(fog_plane,1,0,0,normal),
          "Disabled fog and degenerate falloff must remain native");
    const float forward_fog[]{0,0,-1,-.25f};
    for (float zoom:{.25f,1.f,3.f}) {
        const float offset=150*(zoom-1);
        float start=149;
        scattering_start(149,104,offset,start);
        for (float relative_depth:{-100.f,0.f,50.f,200.f}) {
            const float native=std::clamp(150+relative_depth-149,0.f,104.f);
            const float adapted=std::clamp(150*zoom+relative_depth-start,0.f,104.f);
            check(std::fabs(native-adapted)<1e-5f,
                  "Scattering must preserve foreground and distant ramp weights through zoom");
        }
        view_fog_plane(forward_fog,.8f,.01f,offset,normal);
        check(std::fabs(150*zoom*.01f+normal[3]-(150*.01f+forward_fog[3]))<1e-5f,
              "Zoom must preserve fog attenuation at the player");
        check(std::fabs((150*zoom+50)*.01f+normal[3]-(200*.01f+forward_fog[3]))<1e-5f,
              "Zoom must retain the same fog ramp beyond the player");
    }
    float scattering=0;
    check(!scattering_start(149,104,0,scattering) && scattering==149,
          "Native zoom must preserve atmospheric scattering exactly");
    check(!scattering_start(149,0,300,scattering) &&
          !scattering_start(149,104,std::numeric_limits<float>::infinity(),scattering),
          "Invalid scattering parameters must retain native uniforms");
    check(camera_space(0x7a88a4)==CameraSpace::world && camera_space(0x7a8830)==CameraSpace::drum,
          "Both field/drum matrix branches must receive the orbit in their own coordinate spaces");
    check(camera_space(0x376d224)==CameraSpace::other,"Minimap camera must remain outside orbit scope");
    check(outdoor_projection(2.557646f,100000,.541052f),"The live camera303 31-degree field projection must be recognized");
    for (float degrees : {20.f, 31.f, 60.f, 80.f, 83.f})
        check(outdoor_projection(2.557646f,100000,degrees*pi/180),
              "Outdoor projection selection must support every configured lens and native title adjustment");
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
    for (int yaw=0;yaw<360;yaw+=30) for (float elevation:{.1f,.6f,1.4f}) for (float radius:{60.f,180.f,450.f})
    for (float degrees:{20.f,31.f,80.f,83.f}) for (float aspect:{4.f/3,16.f/9,32.f/9}) {
        const float a=yaw*pi/180, h=radius*std::cos(elevation);
        const float tangent=std::tan(degrees*pi/360);
        const float eye[]{h*std::sin(a),radius*std::sin(elevation),h*std::cos(a)};
        Bounds bound;
        check(view_bounds(eye,ground_target,tangent,aspect,0,0,690,-48,72,20,bound),"Swept footprint must be valid");
        for (int x=-690;x<=690;x+=46) for(int z=-690;z<=690;z+=46) for(float y:{-48.f,0.f,72.f}) {
            const float dx=x-eye[0],dy=y-eye[1],dz=z-eye[2];
            const float depth=-(dx*eye[0]+dy*eye[1]+dz*eye[2])/radius;
            const float sx=std::cos(a)*dx-std::sin(a)*dz;
            const float sy=-std::sin(elevation)*(std::sin(a)*dx+std::cos(a)*dz)+std::cos(elevation)*dy;
            if (depth>=1 && depth<=radius+690 && std::fabs(sx)<=depth*tangent*aspect && std::fabs(sy)<=depth*tangent)
                check(x>=-bound.left-.01f && x<=bound.right+.01f && z>=-bound.far-.01f && z<=bound.near+.01f,
                      "Visibility must contain every projected sample across yaw, pitch, zoom, FOV, aspect and height");
        }
    }
    // Default/title camera303 pose from the live trace. No user orbit is
    // necessary for its shallow view to exceed native surface job bounds.
    const float title_eye[]{725,57.6f,1026.2f}, title_target[]{725,8,890};
    Bounds title_view;
    check(view_bounds(title_eye,title_target,.306f,1.778f,0,0,690,-40,80,20,title_view),
          "Default title camera must provide a surface footprint");
    const std::array<int32_t,4> native_surface{52,49,92,98};
    const auto title_tiles=expand_surface_tiles(native_surface,725,890,title_view);
    check(title_tiles[0]<native_surface[0] && title_tiles[2]>native_surface[2] && title_tiles[1]<native_surface[1],
          "Unmodified orbit must expand both title screen corners and distant surface");
    for (int yaw=0;yaw<360;yaw+=15) {
        const auto tiles=expand_surface_tiles(native_surface,725,890,rotate_bounds(title_view,yaw*pi/180));
        check(tiles[0]<=native_surface[0] && tiles[1]<=native_surface[1] &&
              tiles[2]>=native_surface[2] && tiles[3]>=native_surface[3],
              "Camera footprint must never reduce native surface coverage at any heading");
    }
    const std::array<int32_t,4> negative_tiles{-100,-100,100,100};
    check(expand_surface_tiles(negative_tiles,-5,-5,Bounds{20,20,20,20})==negative_tiles,
          "A smaller camera footprint must preserve an existing larger rectangle");
    check(!view_bounds(ground_eye,ground_target,0,1,0,0,300,0,0,0,footprint),"Invalid projection must use fallback");
    float horizon_height = -1;
    check(sky_horizon_height(-pi/6, 0, .3f, horizon_height) == false,
          "Unexpected sky tilt must retain native lookup");
    check(sky_horizon_height(-.1f, 3.f, .3f, horizon_height), "Native sky scale and tilt must be accepted");
    // On the 600-unit sphere, the lookup equator is depressed by .1 radian
    // at every azimuth, with the native height offset retained.
    for (int azimuth = 0; azimuth < 360; azimuth += 15) {
        const float angle = azimuth*pi/180.f;
        const float x = 600.f*std::cos(.1f)*std::sin(angle);
        const float z = 600.f*std::cos(.1f)*std::cos(angle);
        const float y = -600.f*std::sin(.1f) + horizon_height - 3.f;
        check(std::fabs(std::atan2(y, std::hypot(x,z))) < .00001f,
              "Sky lookup horizon must be equally depressed in every direction");
    }
    horizon_height = 42;
    check(!sky_horizon_height(0,0,0,horizon_height) && horizon_height == 42,
          "Invalid sky scale must fail without changing the destination");
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
