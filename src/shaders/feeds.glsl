//! kFeeds
//! The research jets' camera feeds: the atlas of the cameras' pictures and each camera's frame.
// research jet displays: each shows the picture of a camera on the airframe (camera_feeds.cpp, feed_cameras.h)
uniform sampler2D uFeedTex; uniform int uFeedOn;
uniform vec4 uFeedTile[13], uFeedR[13], uFeedU[13], uFeedB[13];   // atlas tile | right + tanX | up + tanY | back + has a picture
uniform float uFeedSkip;   // drawing a camera's picture: its lens sits just outside the skin, the airframe march starts past it
