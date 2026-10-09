## What's new

### Environment (Codex's review branch, merged)
- Ground and scenery look better without drawing more: pitched roofs lit the right way round, hidden shop and apartment faces removed, roof equipment and canopies on near apartments, flat roofs varied, road and runway markings filtered so they no longer shimmer, terrain normals corrected, and the 2 km bump step smoothed away.
- Distant rock outcrops, spires and sea stacks now match their close-up shapes.
- Broadleaf trees' leaf cut-outs are cheaper to draw with the same coverage.

### XR-40
- The bridge behind the centre display no longer cuts across the two side screens: it is recessed 35 mm, with the screens where they were.

### Performance
- Scenery is drawn nearest first in every direction, so buildings hidden behind nearer ones are no longer shaded. Before, flying on some headings drew a city back to front.
- The cockpit displays draw only the pages the cockpit actually shows: 5 of 8 in the XR-40 and 2 of 8 in the glass cockpits, instead of all 8. Every screen looks and updates the same as before.

### Diagnostics
- The XR-40 storm scene over the city now flies over the city centre and stays there, so every measurement in the analysis sees the same view. Before, it hovered and climbed away during the run.
- The analysis now gives each pass's GPU time from the GPU's own timers, alongside the older numbers that include CPU waits.
- It also times the frame with one piece of work left out at a time (scenery, terrain, the airframe march, the cockpit's shading), to show exactly where the time goes on your GPU. The picture looks wrong for a moment during each of these checks; that is expected.
