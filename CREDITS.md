# Credits

## Samples (all CC0 1.0 Universal, a public domain dedication: free to redistribute inside this plugin)
- **Acoustic: Shinyguitar** by Karoryfer Lecolds (D. Smolken). An archtop recorded through its magnetic pickup and a
  microphone at once. https://github.com/sfzinstruments/karoryfer.shinyguitar (commit `57243cc`)
- **Electric: Emilyguitar** by Karoryfer Lecolds (D. Smolken). An Epiphone (SG-style) with flatwound strings, recorded
  direct. https://github.com/sfzinstruments/karoryfer.emilyguitar
- **Folk Harp, Concert Harp, Strumstick** from the **Versilian Community Sample Library (VCSL)** by Versilian Studios.
  https://github.com/sgossner/VCSL
- **Violin, Violin Pizz** from **VSCO 2 Community Edition** (Solo Violin: arco with vibrato, pizzicato) by Versilian
  Studios. https://github.com/sgossner/VSCO-2-CE (the bowed notes get a crossfaded loop so held notes sustain)

`tools/pack_samples.py` repacks only the WAV files and their note / velocity mapping (trimmed, 16-bit, losslessly
compressed). None of the sources' GUI artwork, photos or signed bank files are used.

## Font
The "Fretty" wordmark is set in **Yellowtail** by Astigmatic (Apache License 2.0, `vst/fonts/LICENSE-Yellowtail.txt`),
baked into the page artwork with a chrome finish.

## Platform
Built with [sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (its VST2 wrapper and build tools).
