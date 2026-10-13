// The indoor preview's room is finite, consistently wound, bounded geometry.
#include "../src/hangar_preview.h"
#include "../src/aircraft_preview_bounds.h"
#include "../src/menu_layout.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void check(bool ok, const char* message) { if (!ok) { fprintf(stderr, "%s\n", message); std::exit(1); } }
static bool near(float a, float b) { return fabsf(a-b)<.0001f; }
struct BoxBounds {
  vec3 lo, hi;
  vec3 size() const { return hi-lo; }
  vec3 centre() const { return (lo+hi)*.5f; }
};
// Inspect the actual emitted triangles, not a duplicate furniture layout. Each
// closed box has six two-triangle faces; palette colours identify its material.
static std::vector<BoxBounds> boxesWithColour(const std::vector<hangarPreview::Vertex>& vertices, vec3 colour) {
  check(vertices.size()%36==0,"Hangar boxes must have six complete faces");
  std::vector<BoxBounds> boxes;
  for (size_t i=0;i<vertices.size();i+=36) {
    if (length(vertices[i].colour-colour)>.0001f) continue;
    BoxBounds b{vertices[i].p,vertices[i].p};
    for (size_t j=i;j<i+36;++j) {
      const auto p=vertices[j].p;
      check(length(vertices[j].colour-colour)<.0001f,"Box material must cover all faces");
      b.lo=vec3(std::min(b.lo.x,p.x),std::min(b.lo.y,p.y),std::min(b.lo.z,p.z));
      b.hi=vec3(std::max(b.hi.x,p.x),std::max(b.hi.y,p.y),std::max(b.hi.z,p.z));
    }
    boxes.push_back(b);
  }
  return boxes;
}
static void dimensions(const BoxBounds& b, vec3 expected, const char* message) {
  check(length(b.size()-expected)<.0001f,message);
}
static void serviceClearance(const BoxBounds& b, float size) {
  check(b.lo.y>=0 && b.hi.y<.60f*size,"Furniture must sit above the floor and below the roof beams");
  check(b.lo.x>-1.35f*size+.12f && b.hi.x<1.35f*size-.12f,
        "Furniture must not intersect either side wall");
  check(b.lo.z>-1.6f*size && b.hi.z<.988f*size,
        "Furniture must remain inside the floor and in front of the rolling doors");
  // An s-by-s footprint encloses the span and fuselage at every yaw. Check
  // against its circumscribed circle, including a half-metre service margin.
  const float x=b.lo.x>0?b.lo.x:(b.hi.x<0?-b.hi.x:0);
  const float z=b.lo.z>0?b.lo.z:(b.hi.z<0?-b.hi.z:0);
  check(std::hypot(x,z)>size*std::sqrt(.5f)+.5f,
        "Furniture must clear the entire rotating aircraft footprint");
}
static void dimensionTests() {
  std::vector<float> mesh;
  for (const vec3 p:{vec3(-5.2f,.7f,1.4f),vec3(5.4f,.7f,1.4f),vec3(0,-.1f,-5.48f),vec3(0,.45f,5.45f)})
    for (const float value:{p.x,p.y,p.z,0.f,1.f,0.f,0.f,0.f}) mesh.push_back(value);
  const auto bounds=aircraftPreviewBounds(mesh);
  check(bounds.valid && near(bounds.left.x,-5.2f) && near(bounds.right.x,5.4f) &&
        near(bounds.nose.z,-5.48f) && near(bounds.tail.z,5.45f),"Preview bounds must measure actual asymmetric uploaded vertices");
  check(near(bounds.left.y,.7f) && near(bounds.right.z,1.4f) && near(bounds.nose.y,-.1f) && near(bounds.tail.y,.45f),
        "Measured extrema must retain their original witness positions");
  check(!aircraftPreviewBounds({}).valid && !aircraftPreviewBounds({1.f,2.f,3.f}).valid &&
        !aircraftPreviewBounds(std::vector<float>(8,0)).valid,"Empty, malformed and degenerate exterior meshes must have no dimensions");
  for (const float bad:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    auto invalid=mesh; invalid[18]=bad;
    check(!aircraftPreviewBounds(invalid).valid,"Nonfinite exterior positions must not produce dimension labels");
  }
  hangarPreview::DimensionInput in;
  in.minX=bounds.left.x; in.maxX=bounds.right.x; in.noseZ=bounds.nose.z; in.tailZ=bounds.tail.z;
  in.wingTipY=.7f; in.wingTipZ=1.4f; in.noseY=-.1f; in.tailY=.45f;
  const auto actual=hangarPreview::dimensionLayout(in);
  check(actual[0].label=="WINGSPAN 10.60 m" && actual[1].label=="LENGTH 10.93 m",
        "Dimension text must use actual mesh bounds, not nominal or symmetric catalogue values");
  check(near(actual[0].metres,10.6f) && near(actual[1].metres,10.93f),"Numeric dimensions must preserve measured metres");
  in.visible=false;
  const auto hidden=hangarPreview::dimensionLayout(in);
  check(hidden[0].label.empty() && hidden[1].label.empty() && hidden[0].metres==0 &&
        hangarPreview::dimensionGeometry(in).empty(),"Classified aircraft must expose no labels, values or measuring geometry");
  check(hangarPreview::dimensionGeometry({}).empty(),"Unavailable mesh bounds must not invent dimensions");
  in.visible=true; in.eye.x=std::numeric_limits<float>::quiet_NaN();
  check(hangarPreview::dimensionGeometry(in).empty(),"Invalid camera data must not emit corrupt dimension geometry");
  size_t maxVertices=0;
  for (const float size:{12.f,42.6f}) for (int turn=0;turn<24;++turn) for (const float pitch:{0.f,.15f}) {
    in={}; in.minX=-size*.46f; in.maxX=size*.48f; in.noseZ=-size*.5f; in.tailZ=size*.49f;
    in.wingTipY=size*.03f; in.wingTipZ=size*.08f; in.noseY=-size*.01f; in.tailY=size*.02f;
    in.position=vec3(0,size*.08f,0); in.eye=vec3(size*.8f,size*.30f,-size*.95f);
    in.cameraRight=normalize(cross(in.position-in.eye,vec3(0,1,0)));
    const quat rotation=quat::axisAngle(vec3(0,1,0),turn*15*DEG)*quat::axisAngle(vec3(1,0,0),pitch);
    in.spanAxis=rotation.rotate(vec3(1,0,0)); in.upAxis=rotation.rotate(vec3(0,1,0)); in.lengthAxis=rotation.rotate(vec3(0,0,1));
    const auto layout=hangarPreview::dimensionLayout(in);
    const vec3 cameraForward=normalize(in.position-in.eye), cameraUp=cross(in.cameraRight,cameraForward);
    auto expected=[&](vec3 p) { const vec3 q=in.position+rotation.rotate(p); return vec3(q.x,hangarPreview::kDimensionFloorY,q.z); };
    check(length(layout[0].witnessA-expected(vec3(in.minX,in.wingTipY,in.wingTipZ)))<.0001f &&
          length(layout[0].witnessB-expected(vec3(in.maxX,in.wingTipY,in.wingTipZ)))<.0001f &&
          length(layout[1].witnessA-expected(vec3(0,in.noseY,in.noseZ)))<.0001f &&
          length(layout[1].witnessB-expected(vec3(0,in.tailY,in.tailZ)))<.0001f,
          "Floor witnesses must track the real parked aircraft transform and asymmetric bounds");
    const auto vertices=hangarPreview::dimensionGeometry(in); maxVertices=std::max(maxVertices,vertices.size());
    check(!vertices.empty() && vertices.size()%6==0 && vertices.size()<400,"Filtered dimension text must use a small quad budget, not per-pixel stencil geometry");
    size_t glyphVertices=0, rulerVertices=0;
    for (size_t i=0;i<vertices.size();++i) {
      const auto& a=vertices[i];
      check(std::isfinite(a.p.x+a.p.y+a.p.z) && near(a.p.y,hangarPreview::kDimensionFloorY) && (a.kind==4 || a.kind==5),
            "Dimensions must remain finite horizontal floor paint with a depth-safe offset");
      if (a.kind==5) {
        ++glyphVertices;
        check(std::isfinite(a.n.x+a.n.z) && a.n.x>=0 && a.n.x<=1 && a.n.z>=0 && a.n.z<=1,
              "Floor glyph UV coordinates must stay within the shared SDF atlas");
      } else {
        ++rulerVertices;
        check(near(a.n.y,1.f) && near(a.n.x,0) && near(a.n.z,0),"Ruler geometry must retain its physical floor normal");
      }
      check(fabsf(a.p.x)<1.35f*size-.12f && a.p.z<.988f*size && a.p.z>-1.6f*size,
            "Dimension paint must stay inside the room without intersecting walls or doors");
      if (i%3==0) check(cross(vertices[i+1].p-a.p,vertices[i+2].p-a.p).y>0,"Dimension and glyph triangles must face up and remain nondegenerate");
    }
    std::string expectedGlyphs;
    for (const auto& d:layout) for (const std::string* text:{&d.name,&d.value})
      for (const unsigned char ch:*text) if (ch!=' ') expectedGlyphs+=ch;
    check(glyphVertices==expectedGlyphs.size()*6 && rulerVertices==72,
          "Each visible name/value character must have one SDF quad, with twelve ruler/leader quads");
    size_t glyph=0;
    for (size_t i=0;i<vertices.size();i+=6) {
      if (vertices[i].kind!=5) continue;
      const int gi=expectedGlyphs[glyph++]-32;
      const float u0=float((gi%16)*FONT_CELLW)/FONT_W, v0=float((gi/16)*FONT_CELLH)/FONT_H;
      const float u1=u0+float(FONT_CELLW)/FONT_W, v1=v0+float(FONT_CELLH)/FONT_H;
      float loU=1, hiU=0, loV=1, hiV=0;
      for (size_t j=i;j<i+6;++j) {
        const auto& q=vertices[j];
        check(q.kind==5,"Both triangles of a glyph must sample the atlas");
        loU=std::min(loU,q.n.x); hiU=std::max(hiU,q.n.x); loV=std::min(loV,q.n.z); hiV=std::max(hiV,q.n.z);
        const vec3 delta=q.p-vertices[i].p;
        check(dot(delta,layout[0].textRight)*(q.n.x-vertices[i].n.x)>=-.00001f &&
              dot(delta,layout[0].textUp)*(q.n.z-vertices[i].n.z)<=.00001f,
              "SDF glyph UVs must remain upright and unmirrored on the world-space floor");
      }
      check(near(loU,u0) && near(hiU,u1) && near(loV,v0) && near(hiV,v1),
            "Floor text must sample the exact atlas character for each measured label");
    }
    for (const auto& d:layout) {
      check(dot(d.textRight,in.cameraRight)>.99f && dot(d.textUp,in.eye-in.position)<0,
            "Floor labels must face the viewer throughout a complete aircraft rotation");
      check(dot(d.textUp,cameraUp)>0,"Top glyph rows must project above bottom rows, never upside down");
      const vec3 relative=d.labelCentre-in.position;
      check(fabsf(dot(relative,d.textRight))<size*.18f && dot(relative,d.textUp)<0,
            "Both captions must stay together in the central foreground instead of behind menu sidebars");
      check(d.cellHeight>d.cellWidth && d.cellWidth*7.f>size*.015f && d.cellWidth*7.f<size*.035f,
            "Floor typography must remain restrained while compensating for the low showroom camera");
      const vec3 toEye=in.eye-d.labelCentre;
      const float labelForeshorten=clampf(fabsf(toEye.y)/length(toEye),.18f,1.f);
      check(fabsf(d.cellWidth/d.cellHeight-labelForeshorten)<.02f,
            "Foreshortening must be measured at the foreground lettering instead of the distant aircraft");
    }
    check(dot(layout[1].labelCentre-layout[0].labelCentre,in.cameraRight)>0,
          "The wingspan caption must consistently read to the left of the length caption");
  }
  std::string shader=hangarPreview::kFS;
  shader.erase(std::remove_if(shader.begin(),shader.end(),[](char c){return c==' ' || c=='\n' || c=='\r' || c=='\t';}),shader.end());
  check(shader.find("texture(uFont,vN.xz)")!=std::string::npos && shader.find("fwidth(distance)")!=std::string::npos &&
        shader.find("smoothstep(.5-aa,.5+aa,distance)")!=std::string::npos,
        "Floor text must use derivative-filtered SDF coverage, never hard pixel-stencil edges");
  check(shader.find("vKind==5?vec3(0,1,0)")!=std::string::npos &&
        shader.find("mix(floorColour(vec3(.22,.26,.29)),c,coverage)")!=std::string::npos &&
        shader.find("rough=mix(.43,.65,coverage)")!=std::string::npos,
        "SDF edge pixels must restore the floor normal and blend into the exact concrete material");
  printf("Hangar dimensions: actual mesh extrema, exact values, classified privacy, parked transform and room clearance across 96 poses; <=%zu vertices: PASS\n",maxVertices);
}
int main() {
  for(const auto size:{std::pair<int,int>{1920,1080},{1280,720},{960,540},{800,600},{640,480}}){
    const float W=size.first,H=size.second;const auto L=loadingLayout(W,H);
    check(L.x>=0&&L.width>0&&L.x+L.width<=W&&L.y+L.height<=H,"Loading strip must remain on screen");
    check(L.y>=H*.73f&&L.height<H*.25f,"Loading strip must leave the aircraft photograph unobstructed");
    check(L.artTop<L.artBottom&&L.artBottom<L.y&&L.artBottom-L.artTop>H*.55f,"Loading fallback camera must have a clear aircraft stage");
  }
  printf("Loading strip: on-screen bounds and clear central aircraft region at five window sizes: PASS\n");
  size_t count = 0;
  // Small training/touring craft, research jets, transport and Atlas A180
  // (42.60 m maximum span/length), plus a larger-room regression case.
  for (float size : {12.f, 16.5f, 24.f, 42.6f, 50.f}) {
    const auto v = hangarPreview::geometry(size);
    check(!v.empty() && v.size()%3 == 0, "Hangar must contain whole triangles");
    check(v.size() < 5000, "Hangar geometry exceeded its small draw budget");
    if (count) check(count == v.size(), "Hangar tessellation must not grow with aircraft size");
    count = v.size(); bool floor = false, fixtures = false;
    for (size_t i=0; i<v.size(); ++i) {
      const auto& a=v[i];
      check(std::isfinite(a.p.x+a.p.y+a.p.z), "Non-finite vertex");
      check(fabsf(length(a.n)-1.f)<1e-5f, "Invalid normal");
      check(a.p.y >= -.361f && a.p.y <= .72f*size+.101f, "Room height outside bounds");
      floor |= a.kind == 1.f && a.p.y == 0.f;
      fixtures |= a.kind == 3.f;
      if (i%3==0) check(dot(cross(v[i+1].p-a.p,v[i+2].p-a.p),a.n)>0.f,"Inward or degenerate triangle winding");
    }
    check(floor && fixtures, "Hangar floor or emissive fixtures missing");
    const auto cabinets=boxesWithColour(v,vec3(.11f,.24f,.30f));
    const auto drawers=boxesWithColour(v,vec3(.42f,.50f,.53f));
    const auto crates=boxesWithColour(v,vec3(.25f,.29f,.28f));
    const auto stripes=boxesWithColour(v,vec3(.58f,.37f,.09f));
    check(cabinets.size()==1 && drawers.size()==3 && crates.size()==3 && stripes.size()==2,
          "Fixed-scale furniture and both wall safety stripes must be present");
    const auto& cabinet=cabinets[0];
    dimensions(cabinet,vec3(2.2f,.85f,.76f),"Workbench cabinet must retain its real metre dimensions");
    check(near(cabinet.lo.y,0),"Workbench cabinet must stand on the tyre-contact floor");
    serviceClearance(cabinet,size);
    int worktops=0;
    for (const auto& b:boxesWithColour(v,vec3(.055f,.085f,.105f))) {
      if (!near(b.centre().x,cabinet.centre().x) || !near(b.centre().z,cabinet.centre().z)) continue;
      ++worktops;
      dimensions(b,vec3(2.4f,.05f,.85f),"Workbench top dimensions must not scale with the aircraft");
      check(near(b.lo.y,cabinet.hi.y) && near(b.hi.y,.90f),"Workbench surface must remain 0.90 m above the floor");
      check(b.lo.x<cabinet.lo.x && b.hi.x>cabinet.hi.x && b.lo.z<cabinet.lo.z && b.hi.z>cabinet.hi.z,
            "Workbench top must still overhang the cabinet");
      serviceClearance(b,size);
    }
    check(worktops==1,"Workbench must have exactly one fixed-scale top");
    for (size_t i=0;i<drawers.size();++i) {
      const auto& b=drawers[i];
      dimensions(b,vec3(1.8f,.036f,.056f),"Drawer handles must retain their real metre dimensions");
      check(near(b.centre().x,cabinet.centre().x) && b.lo.x>cabinet.lo.x && b.hi.x<cabinet.hi.x,
            "Drawer handles must stay centred on their cabinet");
      check(b.lo.z<cabinet.lo.z && b.hi.z>cabinet.lo.z && b.lo.y>0 && b.hi.y<cabinet.hi.y,
            "Drawer handles must remain attached to the cabinet front");
      if (i) check(near(b.centre().y-drawers[i-1].centre().y,.24f),"Drawer spacing must stay fixed in metres");
      serviceClearance(b,size);
    }
    for (size_t i=0;i<crates.size();++i) {
      const auto& b=crates[i];
      dimensions(b,vec3(.8f,.9f,1.f),"Storage crates must retain their real metre dimensions");
      check(near(b.lo.y,0),"Storage crates must stand on the tyre-contact floor");
      if (i) check(near(b.lo.x-crates[i-1].hi.x,.3f),"Storage crate spacing must stay fixed in metres");
      serviceClearance(b,size);
    }
    for (const auto& b:stripes) {
      check(near(b.centre().y,1.2f) && near(b.size().y,.16f),"Wall safety stripes must stay at human height and fixed thickness");
      check(near(b.size().x,.032f) && near(b.size().z,3.2f*size),"Safety stripes must still follow the room walls");
    }
  }
  printf("Hangar geometry: %zu vertices, finite positions, unit normals, outward winding, fixed budget: PASS\n",count);
  // The floor is shaded procedurally; these world-coordinate expressions must
  // remain metric while only the aircraft's stand envelope adapts to uSize.
  const std::string shader=hangarPreview::kFS;
  check(shader.find("vP.xz/3.0+.5)-.5)*3.0")!=std::string::npos,"Concrete floor slabs must remain 3 m apart");
  check(shader.find(".64*uSize,.035)")!=std::string::npos && shader.find(".63*uSize,.035)")!=std::string::npos,
        "Stand paint must remain 70 mm wide regardless of aircraft size");
  check(shader.find("stripe(vP.x,.025)")!=std::string::npos,"Lead-in paint must remain 50 mm wide");
  check(shader.find("stripe(vP.z+.42*uSize,.04)*float(abs(vP.x)<1.8)")!=std::string::npos,
        "Stop bar must remain 80 mm wide and 3.6 m long");
  printf("Hangar scale: metric workbench, drawers, crates, safety stripes and floor cues; all-yaw service clearance across five room sizes: PASS\n");
  dimensionTests();
}
