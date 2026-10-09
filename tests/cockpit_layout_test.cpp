// Data/cache/fit contract for the actual model-owned flight-deck layouts and zoom surfaces.
#include "../src/models.h"
#include "../src/aircraft.h"
#include "../src/cockpit_layout_data.h"
#include "../src/cockpit_focus_zoom.h"
#include <cstdio>
#include <cstring>
int main(){
  int checks=0,failures=0;
  auto test=[&](bool pass,const char* what,int model){++checks;if(!pass){++failures;printf("FAIL model%d: %s\n",model,what);}};
  for(int model=0;model<=kWraith;model++){
    const ModelDef& m=kModels[model];float z=m.eye.z-(m.cockpit==2?.85f:.68f);
    float f[4],s[2],f2[4],s2[2];modelCabinFit(model,z,f,s);modelCabinFit(model,z,f2,s2);
    test(!memcmp(f,f2,sizeof f)&&!memcmp(s,s2,sizeof s),"deterministic cabin fit/cache data",model);
    for(float v:f)test(std::isfinite(v),"finite foot fit",model);
    for(float v:s)test(std::isfinite(v),"finite seat fit",model);
    if(model<10){
      test(f[0]<m.eye.y-.5f,"pedal contacts below hand/yoke band",model);
      test(f[1]<m.eye.z-.5f,"pedals forward in real footwell",model);
      test(s[0]+.028f<=m.eye.y-s[1]-.055f,"seat support extends from floor to pan",model);
      float data[36];packCockpitLayout(model,data);
      test(!memcmp(data,kCockpitLayouts[model].value,sizeof data),"exact bounded authored record packing",model);
      for(float v:data)test(std::isfinite(v),"finite layout data",model);
      test(data[3]>0&&data[7]>0&&data[11]>0&&data[15]>0,"positive instrument/module scale",model);
      const float neutralYoke=z+data[5*4+1]+(model<=2?0.f:.22f);
      test(neutralYoke-f[1]>.19f,"pedals distinctly ahead of primary control",model);
      if(model==2){
        test(fabsf(m.eye.x+.250f)<1e-6f&&fabsf(m.eye.y-.530f)<1e-6f,"Bushmaster matched seated eye",model);
        test(fabsf(f[2]-.252f)<1e-6f&&fabsf(f[3]-.080f)<1e-6f,"Bushmaster preserves pedal spread",model);
        test(fabsf(f[0]+.326259047f)<2e-6f&&fabsf(f[1]+1.8950001f)<2e-6f,"Bushmaster preserves pedal world height/station",model);
        test(fabsf(neutralYoke+1.500f)<2e-6f,"Bushmaster floor-stick station",model);
        test(fabsf(s[1]-.640f)<1e-6f,"Bushmaster supported cushion drop",model);
      }
    }
    CockpitFocusTarget target[24];int n=modelCockpitFocusTargets(model,target,24);
    test(n>0 && n<=24,"bounded focus surfaces for each flyable craft",model);
    for(int i=0;i<n;i++){
      test(std::abs(length(target[i].normal)-1.f)<1e-5f,"unit display normal",model);
      test(dot(m.eye-target[i].center,target[i].normal)>0.f,"display faces pilot",model);
      test(target[i].half.x>0 && target[i].half.y>0,"positive visible display bounds",model);
      test(CockpitFocusZoom::proximity(m.eye,normalize(target[i].center-m.eye),target[i])<1e-3f,"actual layout center focuses without FOV feedback",model);
    }
  }
  test(modelCockpitFocusTargets(-1,nullptr,0)==0,"invalid/no-capacity target request",-1);
  printf("%s: %d cockpit layout/fit/focus checks\n",failures?"FAIL":"PASS",checks);return failures?1:0;
}
