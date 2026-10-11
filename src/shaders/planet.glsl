//! kPlanet
//! The round world (planet.h): the flat world - the islands and their copies, repeating every 100 km - laid over a
//! sphere of radius uPlanetR about the point under the camera. Every place keeps its distance from that point (an
//! azimuthal equidistant wrap), so what is near is drawn as it is and the curve grows with the distance: the horizon
//! dips as the camera climbs, and from high enough the islands repeat across a round world. The physics, the scenery,
//! the G-buffer's normals and materials all stay in the flat world: only where a thing is drawn moves (the vertex
//! shaders), and the lighting pass takes each drawn point back to the flat world for its lookups. uPlanetR 0: flat.
//! Only in the programs that draw the round world (ROUND_WORLD defined: terrain, sea, scenery, sprites, clouds,
//! lighting); elsewhere - the aircraft bodies' bake above all - it is no text at all, and the shared code that uses it
//! (light_common's airDepth, clouds' cloudLayer) takes its flat-world way. Guarded: a program may take it twice.
#ifdef ROUND_WORLD
#ifndef PLANET_GLSL
#define PLANET_GLSL
uniform float uPlanetR;
// sin x / x and atan x / x: their series where x is small (a place a few km off is a few hundredths of a degree round
// the planet, where a GPU's sin and atan are good to a part in 10^5 of their argument - half a metre in kilometres)
float planetSinc(float x){ float x2 = x*x; return x2 < 0.01 ? 1.0 - x2/6.0*(1.0 - x2/20.0) : sin(x)/x; }
float planetAtanc(float x){ float x2 = x*x; return x2 < 0.01 ? 1.0 - x2*(1.0/3.0 - x2*(0.2 - x2/7.0)) : atan(x)/x; }
// a flat-world point: where it is drawn, seen from cam (planet.h planetPos, exactly)
vec3 planetPos(vec3 w, vec3 cam){
  vec2 v = w.xz - cam.xz; float d = length(v);
  if (uPlanetR <= 0.0 || d < 1e-2) return w;
  float th = d/uPlanetR, r = uPlanetR + w.y, k = r/uPlanetR*planetSinc(th), sh = planetSinc(0.5*th);
  return vec3(cam.x + v.x*k, w.y - 0.5*r*th*th*sh*sh, cam.z + v.y*k);
}
// a drawn point: where it is in the flat world (planetPos's inverse; the height as (|q|^2 - R^2)/(|q| + R), which keeps
// a millimetre's precision where |q| - R would lose half a metre)
vec3 planetFlat(vec3 p, vec3 cam){
  vec2 v = p.xz - cam.xz; float hd = length(v);
  if (uPlanetR <= 0.0 || hd < 1e-2) return p;
  float R = uPlanetR, qy = R + p.y;
  float y = (hd*hd + p.y*(p.y + 2.0*R))/(sqrt(hd*hd + qy*qy) + R);
  float k = qy > 10.0*hd ? R/qy*planetAtanc(hd/qy) : R*atan(hd, qy)/hd;
  return vec3(cam.x + v.x*k, y, cam.z + v.y*k);
}
// a direction at the flat-world place w (a normal, the view): as drawn (s = 1), or a drawn one back into w's own level
// (s = -1) - turned through the angle the place lies round the planet, about the horizontal across its bearing
vec3 planetTurn(vec3 n, vec3 w, vec3 cam, float s){
  vec2 v = w.xz - cam.xz; float d = length(v);
  if (uPlanetR <= 0.0 || d < 1e-2) return n;
  vec2 u = v/d; float th = d/uPlanetR, c = cos(th), sn = s*sin(th);
  float a = dot(n.xz, u);   // (the part along the bearing, outwards)
  vec2 across = n.xz - a*u, along = u*(a*c + n.y*sn);
  return vec3(across.x + along.x, n.y*c - a*sn, across.y + along.y);
}
// the height over the sea of the point s along the ray ro + rd s (ro the camera: its nadir the planet's top), exactly
// on the sphere - a straight ray climbs away from a round world - and in the same (|q|^2 - R^2)/(|q| + R) form
float planetAlt(vec3 ro, vec3 rd, float s){
  float yl = ro.y + rd.y*s;
  if (uPlanetR <= 0.0) return yl;
  float h2 = (rd.x*rd.x + rd.z*rd.z)*s*s, qy = uPlanetR + yl;
  return (h2 + yl*(yl + 2.0*uPlanetR))/(sqrt(h2 + qy*qy) + uPlanetR);
}
#endif
#endif
