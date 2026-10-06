## What's new
- **The research jets drawn the fast way.** The XR-30's vectoring nozzles and its gear, and the XR-40's gear and its
  pods' tilt actuators, are now solid parts like every other moving piece. Neither jet needs the slow per-pixel
  pass for its moving parts any more (on the test renderer the XR-30's aircraft pass drops by about two thirds).
  They look the same as before. The first launch after updating rebuilds the aircraft bodies once.
- **Cloaking the XR-40 no longer costs frame rate.** With the cloak on, the craft is still drawn from its mesh, with
  only its cloaked part left see-through for the cloak's shimmer. Before, cloaking switched the whole craft to the slow
  per-pixel pass (about a third of the aircraft pass on the test renderer).
- **Sound follows your default output device.** Switch Windows' default output (say from speakers to a headset) while
  the game runs and the sound moves to it within a second. Before, it kept playing on the old device unless that one
  was unplugged.
