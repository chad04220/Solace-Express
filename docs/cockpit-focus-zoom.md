# Cockpit display focus zoom

In cockpit view, look toward the centre of an instrument or display using right mouse drag or the controller's right stick. The camera smoothly magnifies it as your view approaches the centre, up to 1.8×. Looking away smoothly returns to your chosen manual zoom level.

Mouse-wheel zoom and the hold-to-zoom action continue to work. Automatic focus does not multiply them, change the saved wheel zoom, rotate the view, or move the pilot's eye. Small movements around the centre of a display do not cause zoom jitter. The camera-view windows in the sealed research cockpits remain ordinary windows; the instruments and console pages are focus targets.

Use **Settings → Display focus zoom → Manual zoom only** to switch automatic focus off. This choice is saved with the other settings.

Focus zoom is an optional closer look. Gauges and displays are still intended to be readable at normal cockpit magnification.

## How distance controls the zoom

This is a continuously variable distance-based effect, with a separate smooth camera response. It does not simply trigger a fixed zoom when a screen is selected.

The focal point is the point where the player's unzoomed viewing ray meets a display's physical plane. Its distance from the exact display centre is measured relative to the display's visible half-width and half-height. This accounts for the display's size and orientation, and never uses coordinates from the already-magnified image.

Let `r` be that normalized distance: zero is the centre, and one is the elliptical display edge. With manual zoom at 1×:

- At or beyond the edge (`r >= 1`): 1×
- Between the edge and centre core: a continuous cubic smoothstep from 1× to 1.8×
- Within the small centre core (`r <= 0.12`): 1.8×, preventing tiny aim movements from causing jitter

The exact curve is `1 + 0.8 * (1 - (3*t*t - 2*t*t*t))`, where `t = clamp((r - 0.12)/0.88, 0, 1)`. Both endpoint slopes are zero. Representative values are about 1.029× at `r=0.9`, 1.346× at `r=0.6`, and 1.713× at `r=0.3`.

Where displays overlap in the viewing direction, the strongest of their continuous proximity curves is used. Display-selection hysteresis stabilizes target identity without changing this continuous zoom envelope, so switching selected displays cannot introduce a step in the desired magnification. Temporal easing then smoothly follows that desired value. A higher manually selected zoom remains the floor.
