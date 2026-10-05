// Procedural wheel finish, evaluated once for a visible surface. No new material IDs.
// Local coordinates mirror the existing wheel deployment and steering equations.
vec4 gearWheelFrame(vec3 p, out float halfWidth, out bool braked){
  vec4 G0 = gM[18], G1 = gM[19];
  int kind = int(gM[0].y + 0.5), engine = int(gM[0].z + 0.5);
  bool research = engine >= 5, retract = kind >= 3;
  float up = retract ? 1.0 - gPS.x : 0.0, gh = G1.x, R = gM[0].w;
  float wr = research ? 0.38 : G0.y;
  float upY = -R*0.6;
  if (kind == 3 && !research) { vec2 nsec = nacSection(G0.z); upY = nsec.x - nsec.y + 0.03; }
  float researchLift = engine == 6 ? gh - 0.19 : gh - 0.5;
  float lift = up*(research ? researchLift : upY + gh);
  vec3 mq = vec3(abs(p.x) - G0.x, p.y + gh - wr - lift, p.z - G0.z);
  if (kind == 3 && !research) mq.x = abs(mq.x) - 0.22;
  float mh = research ? 0.13 : kind == 3 ? 0.11 : kind == 0 ? 0.065 : kind == 1 ? 0.09 : kind == 2 ? 0.14 : 0.10;
  float nwr = research ? 0.33 : kind == 3 ? wr*0.75 : wr*0.85;
  float nh = research || kind == 3 ? 0.07 : 0.055;
  vec3 nq = p - vec3(0.0, 0.0, G1.z < 0.5 ? G0.w : G1.y);
  if (!research) nq.xz = rot2(nq.xz, -gPS.z);
  if (G1.z > 0.5 && !research) {
    nwr = 0.10; nh = 0.035;
    nq.y += gh - 0.11*gM[0].x - nwr;
  } else {
    float liftN = up*(research ? researchLift : gh - R*0.6);
    nq.y += gh - nwr - liftN;
    if (research || kind == 3) nq.x = abs(nq.x) - (research ? 0.10 : 0.15);
  }
  braked = dot(mq, mq) < dot(nq, nq);
  halfWidth = braked ? mh : nh;
  return braked ? vec4(mq, wr) : vec4(nq, nwr);
}
Mat gearFinish(vec3 p, int material, Mat m, float footprint){
  if (gPS.x < 0.06) return m;
  float h; bool braked; vec4 f = gearWheelFrame(p, h, braked);
  vec3 q = f.xyz; float r = f.w, rad = length(q.yz);
  if (abs(q.x) > h + 0.065 || rad > r + 0.025) return m;
  float px = max(0.0008, footprint);
  if (material == 6) {
    float channel = 1.0 - smoothstep(h*0.028, h*0.028 + px, abs(abs(q.x) - h*0.32));
    channel *= smoothstep(r*0.76, r*0.9, rad);
    float wall = 1.0 - smoothstep(r*0.006, r*0.006 + px, abs(rad - r*0.77));
    wall *= smoothstep(h*0.65, h*0.9, abs(q.x));
    m.alb = mix(vec3(0.045, 0.047, 0.050), vec3(0.008), max(channel, wall*0.55));
    m.rough = 0.88;
  } else if (rad < r*0.67 && abs(q.x) > h - 0.025) {
    m.alb = vec3(0.55, 0.57, 0.60); m.metal = 0.85; m.rough = 0.28;
    float recess = smoothstep(r*0.25, r*0.25 + px, rad)*(1.0 - smoothstep(r*0.46, r*0.46 + px, rad));
    m.alb *= 1.0 - recess*0.62;
    float angle = atan(q.z, q.y), sector = 1.04719755;
    angle -= sector*floor(angle/sector + 0.5);
    vec2 bolt = rad*vec2(cos(angle), sin(angle)) - vec2(r*0.39, 0.0);
    float head = 1.0 - smoothstep(r*0.045, r*0.045 + px, length(bolt));
    m.alb = mix(m.alb, vec3(0.72), head); m.rough = mix(m.rough, 0.20, head);
    if (braked && q.x < -h && rad > r*0.55) { m.alb = vec3(0.24, 0.25, 0.27); m.rough = 0.55; }
  }
  return m;
}
