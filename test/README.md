# Stress-test assets

## `abs_stress_test_500_podcasts.opml`

Standard OPML containing **500 real podcast RSS feeds** for stress-testing how
VitaABS renders a library with a large number of items.

Feeds were sourced from a real Podcast Addict export
([taext/powercasts](https://github.com/taext/powercasts)), then cleaned
(deduped, dropped direct-media entries, trimmed to 500).

### How to import into Audiobookshelf

1. Create (or pick) a **Podcast** library in ABS.
2. Go to the library, then **New** → **Add Podcasts via OPML file**
   (or Settings → the library's OPML import).
3. Upload `abs_stress_test_500_podcasts.opml`.
4. Review the parsed feed list and confirm the target folder.
5. Optionally disable "auto-download episodes" so the import finishes faster —
   you only need the podcast items themselves to stress-test the client UI.

> ABS fetches every feed during import, so creating all 500 podcasts can take a
> while and a handful of feeds may be dead/unreachable (expected for any real
> feed list). Anything that resolves becomes a library item.

### OPML format notes

ABS's parser (`server/utils/parsers/parseOPML.js`) only reads `<outline>`
elements with `type="rss"` and an `xmlUrl` attribute; the title falls back from
`title` → `text`. This file uses standard OPML 2.0, so it also imports cleanly
into Apple Podcasts, Overcast, Pocket Casts, AntennaPod, etc.
