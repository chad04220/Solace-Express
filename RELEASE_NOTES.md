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
- **XR-20 Mantis cockpit: nothing in the way.** The pilot sat on the centre line of a two-seat cabin, between the seats
  and right behind the centre display, with the stowed nose wheel standing in the cabin beside it: the panel and the
  stick were hidden behind a blue column. The pilot now sits in the left seat like the XR-10's, and a nose wheel
  that stows into the cabin is no longer drawn there from the cockpit.
- **Autoland in gusty winds.** The full autoland sweep (every aircraft at every field it uses, calm, crosswind and
  gusty) now lands every case or declines up front with a reason, where 6 used to go wrong:
  - It flies a slower approach downwind (no gust allowance with the wind behind), raises the flaps after touchdown
    so the brakes grip, and doesn't dive for the glidepath when a gust balloons it near the ground. The Starling's
    overruns at Meadowbrook and the XR-10's hard landings in gusts are gone.
  - It weighs whether a jet can stop on each runway end with the wind and gusts, and whether the terrain lets it get
    down to the glidepath. When neither end is safe (the Starling or XR-20 at Cedar Ridge in a gusty quartering
    wind), it says "unable to autoland" with the reason and holds heading and height, so you can land by hand or
    divert, instead of running off the end or going around into the hills.
