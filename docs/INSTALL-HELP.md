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
| MCC's sound: from `halo1\sound\pc`, `sounds_adpcm.fsb` (sound effects), `sounds_dialogs.fsb`, `sounds_music.fsb`, `sounds_stream.fsb`, `sounds_debug.fsb` (dialogue, music, cinematics: about 1.5 GB) and the `lst` folder | `ux0:data/haloce-vita/mcc/halo1/sound/pc/` |
| Movies (optional, made from the Xbox disc's `bink` folder) | `ux0:data/haloce-vita/movies/` |

You need **one** of the two. Your own **Xbox** files are the recommended
install; Halo: MCC's files are optional and fully supported (both together
also work).

- MCC's files in `ux0:data/haloce-vita/maps/` do **not** work: the game says
  so and where to move them.
- Installed MCC's files before 1.1.3? MCC's dialogue and music need the four
  banks above too: run Setup again (it skips files already there) or copy
  them yourself.
- The Windows Setup tool (`HaloCEVitaInstaller.exe`, on the releases page)
  puts everything in the right place for you: choose **Manual** for your Xbox
  disc image (recommended), **Automatic** for your Steam MCC.

### Xbox files or MCC files: what you get

| | Your own Xbox files (recommended) | Halo: MCC's files (optional) |
| --- | --- | --- |
| Campaign | The 10 missions | The same 10 missions (MCC's Anniversary graphics are not used: the classic look) |
| Campaign sound | Everything | Everything, with MCC's four dialogue and music banks (about 1.5 GB) |
| Multiplayer maps | The Xbox's 13, with the Xbox's own balance | 19: Halo PC's versions of those 13 (Halo PC's tuning of weapons and vehicles, some maps changed) plus Halo PC's Danger Canyon, Death Island, Gephyrophobia, Ice Fields, Infinity and Timberland |
| Online | With players who have the Xbox maps | With players who have MCC's maps: Xbox and MCC players can't play together yet (planned) |
| Intro and other movies | Yes, made from the disc (optional, Setup's Manual mode) | None (they are skipped) |
| Loading screen | The Xbox's picture (from `default.xbe`) | A picture of the menus' live background, taken once from your own files |
| Live background, menus, Forge | Yes | Yes. The plain Classic menu style shows Halo PC's own menus: use Classic+, ElDewrito or Halo 3 |
| MCC campaign mods (Steam Workshop) | - | Experimental: Settings > Multiplayer > Modded maps > MCC mods |

Both installs together also work: the campaign is the Xbox missions (MCC's
copy of a mission is used only when its Xbox map is missing), and
multiplayer has both sets of maps.

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
