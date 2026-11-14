# jumplens

A jump list parser for Windows, in one executable. It walks every fixed and removable volume, finds every
`*.automaticDestinations-ms` and `*.customDestinations-ms` file, decodes the streams and the LNK records inside
them, and shows the result in a table you can search, sort, filter and export.

Written in C++20 with Win32, Direct2D and DirectWrite. No runtime, no installer, no dependencies to download: the
single `jumplens.exe` starts and scans.

made by diquuuu

## What it reads

Automatic destinations (CFB container):

- DestList v1 to v6, including pinned entries, access count, interaction count, last modification time and the
  destination path stored in the entry
- every numbered stream, decoded as a LNK record: target path, arguments, working directory, icon location, volume
  and file identifiers, machine ID, tracker GUIDs, link flags, extra data blocks
- the AppID table maps the 16 character file name prefix to the application that wrote the list

Custom destinations (footer chunked record file):

- the category header (custom category name, frequent, recent, tasks) and the 12 byte chunk header
- carved LNK records, with the trailing shell object class ID trimmed so the target path stays intact
- orphan streams and empty categories are reported, not dropped

Both parsers are tolerant on purpose. A jump list that Windows is still writing, an empty list that only holds the
32 byte DestList header, or a CFB chain that ends past the end of the file produce partial results plus a note
instead of a failure.

## Discovery

The default mode walks every fixed disk, removable volume and RAM disk. There is no folder blacklist, no name
pattern filter and no path heuristic: any file whose name carries the jump list suffix is parsed, wherever it lives.
A second mode limits the walk to the usual `Recent` folders of the current profile and the machine.

Reparse points are skipped only to avoid directory loops, and the walk reports when it reaches its entry budget
instead of silently truncating.

## Using it

```
jumplens.exe                     open the window
jumplens.exe --csv out.csv       scan and write the CSV, no window
jumplens.exe --paths             print the jump list files that would be parsed
jumplens.exe --standard          limit the scan to the usual Recent folders
```

In the window:

- the summary strip counts jump lists, entries, pinned entries and the time range; the histogram is a filter
- the left rail lists every source file ordered by number of entries, and switches between scan modes; drag its
  right edge to widen it
- right-click a row to open a menu: `Open folder` reveals the entry's target in Explorer (`/select` with the file
  selected; the containing folder if the file is gone, or the jump list file itself for shell items), `Copy path`
  copies the target path, `Copy jump list path` copies the source file path
- two filters sit under the scan mode switch:
  - `In instance only` keeps only entries dated since this boot of the PC
  - `Reduce noise` hides entries whose target file is a Windows executable that verifies as Microsoft Windows
    signed. Verification uses Authenticode for embedded signatures and the Windows catalog (SHA256 lookup, SHA1
    member tag) for the rest, so a renamed or replaced file cannot pass as Windows. Entries with no target, or a
    target outside a filesystem path, are never hidden. It ships on
- clicking a column header sorts; dragging a separator in the header resizes that column; `Ctrl+F` focuses the
  search; `Ctrl+E` exports
- selecting a row opens the details drawer with the target, the timestamps, the tracker GUIDs and the source file
- `F5` rescans

## CSV columns

Source file, source type, AppID, application, category, MRU position, entry number, pinned, interactions, entry
created, last modified, hostname, MAC address, target path, arguments, working directory, target created, target
modified, target accessed, file size, attributes, drive type, volume serial, volume label, network name, machine ID,
volume ID, file ID, volume birth, file birth, DestList path, resolved path, IDList path, link flags, show command,
hotkey, icon location, extra blocks, relative path, orphan stream, notes.

The file is UTF-8 with a BOM so Excel opens it correctly, and every field is quoted.

## Building

Requirements: Visual Studio 2022 or newer with the C++ desktop workload, and a Windows SDK.

```
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The script locates `vcvars64.bat`, appends the SDK include and library folders, writes an `args.rsp` and calls `cl`
once. The result is `build\jumplens.exe`. `.\build.ps1 -Tests` builds the console harness in `tests\parse_test.cpp`
instead, which prints one line per file and is how the parsers were checked against a real machine.

## Credits

The AppID to application mapping in `data/appids.txt` comes from Eric Zimmerman's
[JLECmd](https://github.com/EricZimmerman/JLECmd) project, used under the MIT license. The table is vendored as a
plain text file and compiled into the executable by `tools/gen_appids.ps1`.

The logo tile and the toolbar glyphs (`list-magnifying-glass`, `magnifying-glass`, `arrow-clockwise`,
`download-simple`) are [Phosphor Icons](https://phosphoricons.com), used under the MIT license. `data/logo.svg` is
the composed tile and `data/icons/` holds the glyph sources. `tools/gen_logo.ps1` and `tools/gen_glyphs.ps1`
rasterize them into the premultiplied assets baked into the executable; `data/logo.png` and `data/icons/` stay
around as the reproducible sources.

The parsers, the discovery walk and the interface are original C++.

## License

MIT. See `LICENSE`.
