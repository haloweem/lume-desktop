# Native clock typography

Clock typography, positioning, colors, and format follow the original Welfare,
Mond, and Inside Dream Clock.ini files. Original font files and skin provenance
are staged under `ui/assets/fonts` and `ui/assets/sources` respectively. DirectWrite
loads those files into a private collection, including the original OTF fonts.

The original native menus and editors are reused through the independent
`lume-widget-ui` library in `../widget_ui`. Their source attribution and GPL license
are retained in that directory rather than owned by the clock implementation.
