## What's new

Dynamic weather (below), and a fix to the Airline tab.

### Fixes
- The Airline tab's text stays inside its panels. At 1920x1080 the "No routes yet" line ran across into the right-hand panel through the Pilots heading, and the right-hand panel's lines ran off its edge. Long lines now wrap, and the pilots' names stop short of their Hire and Let go buttons.

### Wind and gusts
- Gusts come in bursts. A gust builds over a second or two, peaks at about the reported gust, swings the wind a little and dies away, with a lull before the next. Parked, you meet one every ten or twenty seconds. Flying, you meet them quicker and sharper. The gusts are strongest near the ground and fade with height. Before, the wind wobbled smoothly all the time at the same rate.
- Turbulence is carried by the air. The faster you fly, the quicker the bumps come. Near the ground the eddies are small and choppy, and aloft they are long and smooth. A gust under one wing now rolls the aircraft, and one along the fuselage pitches or yaws it.
- The ground stirs the wind. A strong wind over open land is bumpy low down. Over the sea it is smoother, and over forest, towns and broken hills it is rougher.
- Mountains shape the wind. Where the wind climbs a slope you get lift, and where it pours down the far side you get sink. Below and behind a ridge there are rotors (rough air). On a windy day, expect to be lifted on a ridge's windward face and pushed down in its lee.
- Sunny days with fair-weather cumulus have thermals: rising air under the clouds, gentle sink between them, and bumps on the way up. They fade out near the ground and need some turbulence in the weather, so calm, settled days stay smooth.
- Inside a cumulus it is bumpy. Inside a grey overcast it is fairly smooth. Under a storm cell the air sinks under its rain and spreads out near the ground as a gust front. Inside the cell it rises and is violent.
- The wind turns clockwise with height, about 15 degrees by 1,000 m, as it does in the real world. The tower still reports the surface wind.
- The HUD's wind dial shows the wind averaged over a couple of seconds, so the eddies don't make it flicker.
- Windsocks swing with the gusts as they come through.

### Clouds
- Clouds drift with the wind. Before, they drifted into it.
- Cloud tops lean downwind, because the wind is stronger aloft. The stronger the wind, the more they lean.
- Wisps stream off the clouds' downwind edges. The billows boil upwards slowly: quickly in a sunny day's cumulus, faster still in a storm, hardly at all in an overcast.
- The bumps, the icing and the rain are now where the clouds are drawn. The game reads the same cloud field the renderer draws, so you only pick up ice inside an actual cloud (or its rain), not anywhere between the cloud base and tops.

### Flying through clouds
- An aircraft punches a tunnel through the cloud it flies through. The tunnel opens just behind it, widens and fills in again over the next minute, and drifts with the cloud.
- In and near cloud, wisps stream past the aircraft, so you can feel your speed. The wingtip vortices draw faint ribbons through the cloud.
- Inside a cloud, a fine mist streams across the windscreen.

### Rain
- Rain falls from the clouds. It is heavier under the denser cells and lighter between them, and it stops above the cloud tops. Showers from scattered clouds only fall under those clouds, and the wind carries the rain downwind as it falls.
- Rain shafts: grey curtains of rain hang under the heavier clouds, slanted by the wind. You can see them from far off and fly into them.
- The number of raindrops round you follows how hard it is raining. Drops vary in size and speed, and their streaks follow the wind and gusts.
- At night, the landing lights light up the rain in their beams. Lightning lights up the rain round you.
- Visibility closes in under the heavier cells and opens up between them.
- Windscreen: parked or taxiing, drops sit beaded on the glass and now and then run down. In the airflow they are swept back over the glass, streaming up over the roof through the windscreen and aft out of the side windows, quicker and longer the faster you fly. They only show on the glass, not over the instrument panel.
- The drumming on the airframe follows how hard it is raining and gets louder the faster you fly. The wind noise gusts with the bumps you fly through.
- Snowfall varies with the clouds in the same way, and the flakes catch the landing lights too.

### For developers
- New `src/weather.h` / `weather.cpp`: the wind field (mean wind with profile and veer, gust bursts, frozen-turbulence eddies with their gradients across the airframe, terrain lift, sink and rotors, thermals, storm drafts), and a CPU copy of the cloud pass's density (the same baked noise, drift, lean and boil) with the rain under it. Everything there depends only on place and time, so a flight replays exactly.
- `Weather` carries the cloud field's live state (`cloudDrift`, `cloudDetail`, `cloudBoil`). The game moves it on each frame and it is never saved.
- `flight_test` checks the weather fields: still air is still, the gusts peak near the reported gust, the eddies are as strong as asked, ridges lift and lees sink, rain falls only under cloud, and a replay is exact.
- Debug scenes take their weather from `WX="cover,base m,precip,storm,wind kt,from deg,gust kt,turbulence,time of day"`, or from a `@` suffix on the scene name. The new `wxfly_<x>_<z>_<alt>_<hdg>_<seconds>_<c|k|b>_<aircraft>` scene flies straight and level on the autopilot, then shows the chase view, the cockpit, or a look back along the path flown (the wake through the cloud).
- Autoland sweep with the new wind: every calm and crosswind case lands (580 of 580). In the gusty case (8 m/s, gusting 12, turbulence 0.45), 281 of 290 land, 4 are declined at Cedar Ridge, and 5 fail: Bushmaster at GLS, Q400 at FJH and FAR (off the runway's side), Osprey at FAR (wingtip strike) and Mantis at NPT (hard landing). Before the change all 870 landed. The autopilot is to be tuned for the new wind separately.
