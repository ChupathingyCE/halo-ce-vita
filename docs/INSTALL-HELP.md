# Install help (Halo CE for PS Vita 1.1.x)

The short answers to what people ask most. The full guide is the README's
[Install](../README.md#install) section.

## Where do I get the game?

Only from this project's GitHub releases:
**https://github.com/BirchWoodGod/halo-ce-vita/releases**

Mirrors and stores such as PSVitaAlive or VitaDB are not run by us and are
often several versions behind (1.1.2 can't start from MCC's files alone, for
example). If something in this page doesn't match what you see, check that
your VPK is the newest one from the releases page.

To update, install the new VPK over the old one with VitaShell. Your maps,
saves and settings in `ux0:data/haloce-vita/` stay.

## Which folder is which?

There are two kinds of game files and each has its own folder. Never put one
kind in the other's folder.

| You have | It goes in |
| --- | --- |
| An **Xbox** disc or disc image: its `maps` folder | `ux0:data/haloce-vita/maps/` |
| The Xbox disc's `default.xbe` (the loading screen's picture and sound) | `ux0:data/haloce-vita/default.xbe` |
| **Halo: The Master Chief Collection** on Steam: everything inside `halo1\maps` (the `.map` files and the `custom_edition` folder) | `ux0:data/haloce-vita/mcc/halo1/maps/` |
| MCC's sound effects: `halo1\sound\pc\sounds_adpcm.fsb` and the `lst` folder beside it | `ux0:data/haloce-vita/mcc/halo1/sound/pc/` |
| Movies (optional, made from the Xbox disc's `bink` folder) | `ux0:data/haloce-vita/movies/` |

You need **one** of the two. Your own **Xbox** files are the recommended
install; Halo: MCC's files are optional and fully supported (both together
also work).

- MCC's files in `ux0:data/haloce-vita/maps/` do **not** work: the game says
  so and where to move them.
- The Windows Setup tool (`HaloCEVitaInstaller.exe`, on the releases page)
  puts everything in the right place for you: choose **Manual** for your Xbox
  disc image (recommended), **Automatic** for your Steam MCC.

### What an install from MCC alone has and hasn't

With MCC's files alone (no Xbox `maps` folder) the campaign, multiplayer,
Forge, the menus and their live background all work. Different from an Xbox
install:

- The loading screen shows a picture of the menus' live background (taken
  once from your own files the first time the main menu is up): the Xbox
  loading picture is in the disc's `default.xbe`, which MCC doesn't have.
- No intro or other movies: they come from the Xbox disc (they are
  optional; the game skips them).
- Dialogue and music of MCC's maps are silent in 1.1.x (sound effects play).
- Online, an MCC map plays with players who have the same MCC map.

## I don't have a PC

The game files have to come from your own copy of Halo: an Xbox disc, or
Halo: The Master Chief Collection on Steam. What works today without a
Windows PC:

- **Copying files to the Vita**: VitaShell's FTP (press SELECT in VitaShell)
  works with any FTP app, including phone apps on Android or iOS, on the same
  Wi-Fi. A USB cable to any computer (VitaShell, START, USB connection) works
  too.
- **Steam MCC files**: Steam is needed to download them once (any PC, Mac or
  Steam Deck with Steam). After that, copying them over FTP from that machine
  or a phone is enough.
- **An Xbox disc image**: extract its `maps` folder with extract-xiso (Linux,
  macOS and Windows builds exist) and copy it over FTP.

An install made right on the Vita, with no computer at all, is being worked
on; it isn't in 1.1.x.

## Something's wrong

| What you see | What it is |
| --- | --- |
| Black screen with no sound right at the start, after copying MCC's files | MCC's files are in `maps/`: move them to `mcc/halo1/maps/` (see the table) |
| Stuck on the loading picture, music playing | The shader compiler is missing: run ShaRKF00D once (README, Requirements) |
| The intro is black but you hear it | The movies were converted in a form the Vita can't show. 1.1.3 skips such a movie (`halo.log` says so). Convert them again with Setup, or delete `ux0:data/haloce-vita/movies/` (the movies are optional) |
| The loading screen is black | No Xbox `default.xbe` and no picture of the live background yet: it is taken the first time the main menu's live background is up (ElDewrito menu style) |
| The online menus don't show up | The Halo PC files are missing; the settings panel's Multiplayer tab (SELECT + D-pad down) says which (README, Halo PC files) |

When you report a problem, send `ux0:data/haloce-vita/halo.log` and, after a
crash, the newest `psp2core-*.psp2dmp` from `ux0:data/`.
