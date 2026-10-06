## What's new

- **Much faster launches after the first.** Built aircraft bodies were saved to the cache but never loaded back: a
  size check on the saved file was off by one value, so every launch threw the file away and built all the bodies
  again (about 6.5 minutes for the full set on an RTX 3070 Laptop). They now load from the cache. The game's own
  start-up and every step of `diagnostics.bat` after the first two (which time a cold start on purpose) skip the
  rebuild.
