//! kRtIO
//! Full-screen scene pass I/O: the pixel outputs and the cloud-split switch.
in vec2 vUV;
layout(location=0) out vec4 oColor;
layout(location=1) out float oDepth;
layout(location=2) out float oCloudMask;   // 1: this pixel's clouds are left to the quarter-resolution cloud pass
uniform int uCloudSplit;
