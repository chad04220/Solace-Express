#!/bin/sh
# Asks the render server (render_harness serve) for one shot and prints its reply: "ok <image> <seconds> [pass times]".
#   tools/render_client.sh <scene> [frames=N] [taam=N] [bench=N] [dbgoff=N] [out=path]
# Start the server once (it keeps the shaders, the islands, the meshes and the scenery loaded between shots):
#   SHADERCACHE=<dir> PREWARM=1 build/render_harness serve 1280 720
REQ=${SERVE_REQ:-/tmp/claude-0/sp/rs.req}
REP=${SERVE_REP:-/tmp/claude-0/sp/rs.rep}
[ -p "$REQ" ] || { echo "no render server listening on $REQ" >&2; exit 1; }
echo "$*" > "$REQ"
head -n 1 "$REP"
