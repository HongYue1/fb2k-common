# fb2k-common

Code shared by the foobar2000 components in this workspace (foo_bettertabs,
foo_enhancedplaylisttabs, foo_mediabar, foo_onscreendisplay). No foobar2000 SDK or
Windows dependency: plain C++20, header-only except `src/cover_accent.cpp`.

Components use it as a sibling checkout, like the SDK: add `..\fb2k-common\include` to the
include path and compile `..\fb2k-common\src\cover_accent.cpp` into the component.

| File | What |
| --- | --- |
| `include/fbc/colour.h` | OKLab, sRGB gamut, WCAG 2 and APCA contrast, cover colour -> accent / fill / text |
| `include/fbc/cover_accent.h`, `src/cover_accent.cpp` | The colours of a decoded cover: primary, optional secondary, colourfulness |

## Colour pipeline

1. A worker decodes the cover (WIC, 256 px longest edge, 32bppPBGRA) and calls
   `fbc::cover_colours`. The primary is the most prominent *vivid* colour, judged against
   what its hue can reach in sRGB (so a yellow and a blue of the same visual intensity score
   alike), averaged in lightness, hue and chroma over its neighbourhood.
2. The host turns it into what it draws with, against its real background:
   - `colour::accent_for_background(raw, bg)`: lines, outlines, glyphs, progress. Clears APCA
     Lc 55 on light panels and Lc 45 on dark ones (`accent_min_lc_for(bg)`), keeping as much
     of the cover's chroma as it can.
   - `colour::fill_for_cover(raw, light_fill)` + `colour::text_on(fill)`: solid fills with text.
   - `colour::with_min_lc(rgb, bg, lc)`: nudges a colour the user chose, only as far as needed.
   - `colour::mix(a, b, t)`: OKLab blend, for fading between two accents.

## Tests

`test\build_tests.bat` builds and runs `colour_test` (synthetic covers, APCA references) and
`golden_test` (the user's real covers, listed in the uncommitted `test\local\covers.txt`,
against `test\local\golden.txt`; `build_tests.bat --update` rewrites it after a deliberate
change). Output in `test\tests.out`.

## License

[MIT](LICENSE)
