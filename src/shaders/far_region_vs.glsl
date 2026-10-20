//! kFarRegionVS
//! A bounded lightweight island mesh outlives its full World. Positions are ECEF-relative to its own chart
//! origin. Re-express into the active surface chart, then use the same planetPos/view/log-depth as full terrain.
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColour;
layout(location=3) in float aHeight;
uniform mat4 uRegionTransform, uVP, uPanoView;
uniform vec3 uCamPos;
uniform vec2 uJit, uPano;
uniform float uLogC, uGeoRadius;
out vec3 vColour, vNormal, vPosition;
out float vHeight;
out vec2 vLocal;   // (where it lies on the active chart: its x and z)
void main() {
  vec3 q = (uRegionTransform*vec4(aPosition, 1.0)).xyz;
  float horizontal = length(q.xz);
  float angle = atan(horizontal, uGeoRadius + q.y);
  vec2 bearing = horizontal > 0.001 ? q.xz/horizontal : vec2(0.0);
  vec3 localPosition = vec3(bearing.x*angle*uGeoRadius, max(aHeight, -0.5), bearing.y*angle*uGeoRadius);
  vec3 n = normalize(mat3(uRegionTransform)*aNormal);
  // Undo the chart-origin sphere bend for the deferred pass's flat-world normal convention.
  float along = dot(n.xz, bearing), c = cos(angle), s = sin(angle);
  vec2 across = n.xz - along*bearing;
  vec2 normalXZ = across + bearing*(along*c - n.y*s);
  vNormal = normalize(vec3(normalXZ.x, n.y*c + along*s, normalXZ.y));
  vLocal = localPosition.xz;
  vec3 cp = planetPos(localPosition, uCamPos);
  vPosition = cp; vColour = aColour; vHeight = localPosition.y; // identical clamped sea-floor interpolation/discard to full terrain
  gl_Position = uVP*vec4(cp, 1.0);
  bool behind = false;
  if (uPano.x > 0.0) {
    vec3 p = (uPanoView*vec4(cp, 1.0)).xyz; float a = atan(p.x, -p.z), d = length(p);
    gl_Position = vec4(a/uPano.x*d, p.y/max(length(p.xz), 0.001)/uPano.y*d, 0.0, d); behind = abs(a) > 1.9;
  }
  gl_Position.xy -= 2.0*uJit*gl_Position.w;
  gl_Position.z = (log2(max(1e-6, 1.0 + gl_Position.w))*uLogC - 1.0)*gl_Position.w;
  if (behind) gl_Position.z = 2.0*gl_Position.w;
}
