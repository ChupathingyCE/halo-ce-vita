# Command mode: a commander game type under Forge (design, Nov 5 2026)

Name: **Command mode**, "COMMAND" in the menu. The label lives in one place:
`COMMAND_MODE_NAME` in `port/linux/game/command.h` plus its lang key
(`"COMMAND"` in `port/vita/app0/lang/es.txt`). It is an original mode built
from Halo CE's own units and maps, read at runtime from the player's own
files. It takes nothing from any other game: no name, leaders, units, maps,
UI or look. Nothing of Bungie's or Microsoft's goes in the repo, and no
built or converted level files go on the card.

Owner decisions (Nov 5) are folded in below. Where an item names a
decision, the number refers to section 13.

## 1. What it is

A team game type, picked like CTF or Slayer: **red vs blue**, and **each
team's army mixes marines and the Covenant** (Grunts, Jackals, Elites, and
Hunters as the heavy unit): the owner's change of Nov 5, replacing UNSC vs
Covenant. A squad's team is the squad's, never its species' (the same
mechanism as Forge's AI team: a Grunt of red's fights for red), so one
team's marines and Grunts never fight each other, and each team's AI fights
the other team's marines and Covenant alike. Each species plays its
campaign role: Grunts man the Shades, marines drive and gun Warthogs, Elites
lead squads. Each team has one **commander**; everyone else plays on the
ground as a Spartan, and ground players give no orders (decision 4). The
commander sees the map from above and orders the team's AI squads. **First
person is only the commander's own Spartan** (owner, Nov 5: AI units are
never playable; section 8). When no human takes a team's commander seat, an
**AI commander** plays it (decision 3).

It stays a simple objective game in feel. A variant uses **territories**
(AI squads can hold them), a **flag** (players carry it), or both
(decision 2). The economy only feeds the objective.

**Maps:** Blood Gulch only for now; it is built and proven first. Later,
and not set up yet: Sidewinder, Death Island, Infinity, Ice Fields,
Gephyrophobia, Danger Canyon. Halo CE big maps and Halo CE units only
(decision 5). Halo 2 maps come later.

## 2. The core loop

1. The round starts. Each side's **HQ is the map's own base**, with one
   infantry squad and a little income.
2. **Supply points** are territory circles placed per map. A side takes one
   by having more living units in it than the other side (players and AI
   both count). While a side holds a supply point it earns income every
   few seconds. Each HQ also pays a small trickle.
3. The commander spends resources on **HQ upgrades** (decision 1). An upgrade
   makes the games' own objects appear on the base:
   - **Barracks**: infantry squads can be bought (and arrive faster).
   - **Vehicle bay**: a vehicle spot where Warthogs, Ghosts and Scorpions
     appear, and the UNSC Pelican carries a Warthog.
   - **Defence**: the games' own turrets on the base, AI-manned.
   - **Supply**: more HQ income; crates appear on the base.
   Each upgrade has a cost and a short build time with a progress bar.
   Destroying the objects it placed (turrets, the vehicle spot's vehicle)
   removes their effect until they are rebuilt. Further upgrade levels come
   later.
4. **Reinforcements arrive by dropship** (section 6). A free wave comes
   every 60 s, so a side with no resources is never locked out. Bought
   squads come on the next flight.
5. **Objective**: a side scores while it holds an objective territory, or
   by capturing the flag (players carry it; the commander can carry it after
   dropping into a unit). The first side to the score limit wins, or the
   higher score when time runs out. The HQ can't be destroyed in v1.

### The economy as built (step 4b, Nov 6)

- **Resources**: each side starts with 100. Every 5 s it earns its base's
  trickle (4, or 8 with the supply depot) plus 5 for each supply point it
  holds.
- **Supply points** are the map's own king of the hill hills (their markers
  grouped by hill, as the king game type groups them), minus the hills on
  the bases (within 10 wu of a flag stand): Blood Gulch has 6. A side takes
  one by having more living units in it (squads, Shade crews, Spartans;
  within the hill's spread, at least 5 wu, and 4 wu up or down) for 5 s.
  The ground shows each as a ring: red's blue-green, blue's red, no one's
  white.
- **The HQ panel**: Y / Triangle (F on the keyboard) opens it; the D-pad
  chooses a row, A / Cross buys it (the keys 1 to 9 buy their rows); B /
  Circle closes it. Rows: Marines 40, Grunts 40, Grunts and Jackals 60,
  Elite and Grunts 90, Hunter pair 220 (leaves 60 s after the order), then
  the upgrades: Barracks 150 (20 s to build), Supply depot 100 (15 s),
  Vehicle bay 200 (25 s), Defences 120 (20 s), Armory 150 (20 s). A row says
  why it can't be bought ("Needs the barracks", "Can't afford", "No room
  for more troops", "Being built").
- Squads need the barracks; a bought squad flies in at once (the Hunters
  after their 60 s). With the barracks the side's free waves come every
  45 s instead of 60; each side has its own wave clock.
- **The Spartan's respawn** costs 90 (the first spawn free): the game's own
  respawn is held while red can't pay, and the HUD says "Spartan: 90 to
  respawn, can't afford"; it is paid as he spawns.
- **Blue** (until the AI commander) spends by a plain rule: the barracks,
  then the supply depot, then a squad whenever it can.
- **The upgrades' objects (step 4c)**, the games' own, made again with the
  base's next wave if destroyed or taken (a vehicle driven more than 25 wu
  off is left to whoever has it, and another made):
  - Vehicle bay: Blood Gulch's Warthog and Ghost beside the base, and each
    Pelican carries a Warthog at its "cargo" marker, set down at the hover.
  - Defences: two more Shades on the roof's front corners, Grunt-manned
    (each roof spot must be level about it: a Shade on the edge tipped and
    lost its Grunt).
  - Supply depot: three Covenant supply containers beside the base
    (`scenery\c_storage\c_storage` from b30, in command_covenant's
    recipe: Halo CE's campaign has no UNSC crates), and the double trickle.
  - Armory: the map's sniper rifle and rocket launcher at the flag stand,
    and the Spartan's secondary the commander's pick (rocket launcher by
    default; the HQ's last row switches it to the sniper rifle and back),
    given at once and at every spawn.
  - The spots are the base's own (its flag stand, toward the other base and
    to its sides) until the Command setup's upgrade markers.
- **Hunters** take no seat in Halo CE's dropship: a Hunter pair is made
  under its hovering dropship at the drop (the fallback for any member that
  takes no seat).

### The objectives as built (step 5, Nov 7)

- **The variant**: the Forge lobby's OBJECTIVE row under START COMMAND (A
  goes round): TERRITORIES AND FLAG (the default), TERRITORIES, FLAG. The
  score limit is 300 and the time limit 20 minutes (console: `command
  objective territories|flag|both`, `command score N`, `command time M`).
- **Territories**: the three supply points nearest the middle between the
  bases (Blood Gulch: lettered A, B, C from red's base) are the objective
  territories; the ground shows each with a second ring and its letter. They
  are taken as any supply point (squads, Shade crews and Spartans; the most
  in it for 5 s) and pay as one. A side scores 1 point a second for each it
  holds (Halo 2's Territories: 300 is five minutes of one territory).
- **The flag**: the map's own multiplayer flag at each base's flag stand.
  Only Spartans carry it: a player's unit, or a side's Spartan as AI, never
  a squad's member (one that picks it up drops it). A Spartan takes the
  enemy flag by touching it (1 wu), and scores 100 points by bringing it to
  his own stand while his own flag is home. Its carrier's death drops it; a
  Spartan of its own side touching it, or CE's 17 s untouched, sends it
  home. The CTF announcer's own lines are played (has the flag, score,
  flag returned). Three captures win a flag game.
- **The Spartan's flag run**: the commander orders his Spartan onto the
  enemy flag (the cursor near it) and it gets a FLAG order: it goes to the
  flag wherever it is, takes it, brings it home, and goes again until
  ordered otherwise.
- **The end**: the first side to the score limit wins, or the higher score
  when the time runs out (the same score: a draw). The game ends as the
  game's own do (`game_engine_end_game`: its game-over sound and delay, its
  scores), the HUD saying who won, then the Halo 3 carnage report: RED TEAM
  and BLUE TEAM as the teams (score; the enemy units each killed; the units
  each lost, Spartans included; CAPTURES; TERRITORY time), the commander
  under red (his Spartan's kills and captures, whether he or the AI fought
  it), and under blue, while no human plays it, its AI Commander. The
  game's title is "Command". A local Command game's report goes on to the
  main menu (Forge's own leave), by itself after 30 s.

## 3. The Command setup (placed once per map, saved like a Forge variant)

New Forge marker kinds, made like the SPAWNING category's markers
(forge_spawns.inc: no tag of the map's, saved in the map variant):

| marker | per map | what |
|---|---|---|
| HQ (team) | 1 per side, at the map's base | squads gather here; the commander's camera starts here |
| Landing spot + flight path (team) | 1 per side | where the side's dropship hovers, plus 2-4 path points in and out |
| Upgrade spot (team, kind) | a few per side | where an upgrade's objects appear (turret, crates, vehicle spot) |
| Supply point | 2-6 | a territory circle that pays income while held |
| Objective | 1-3 (territories) / the map's flag stands | scoring |

The game type is offered on a map only when a saved variant of it has the
setup. The map variant version is bumped for the new marker kinds; old
variants still read. Blood Gulch's setup ships as our own data (marker
positions only).

## 4. The map built out at load, in memory

A multiplayer Blood Gulch lacks the AI characters (actor variants, their
units, weapons, animations, sounds), the Pelican and the Covenant dropship,
and some upgrade objects. When a Command game starts, these are taken from
the **player's own Halo CE campaign maps** and added to the loaded map's
tags, **in memory**, the same idea as the Halo 2 direct path: nothing is
shipped, and no map or pack file is written to the card.

- Machinery to reuse:
  - scene_pack.c's builder (`build_tags`: a recipe's root tags and
    everything they reference, from the source map).
  - `scene_pack_place_tags` and scene_packs.c's commit, which put the tags
    after the map's own in the tag table.
  - The Halo 2 kit's `scene_pack_filter`, whose pixels and samples are not
    copied: each stays at its offset in its source and is read from there.
- **Built (Nov 5):** `scene_packs_load_in_memory(name)` builds a recipe
  through `scene_pack_build_many` with a `memory:` pack path. The pack
  comes back as bytes in the build's result, and nothing is written: no
  .pack, no .ok record, no .zidx source index. The game then reads the pack
  through `fmemopen` (a system temporary file on Windows, never the card).
  The recipes are `command_marines` (with dialogue: nothing stripped),
  `command_covenant` (with the Shade turret) and `command_dropships`, all
  our own text. File packs
  build byte-identically to before (scene_pack_test buildmany, compared with
  1.2.0's builder), and a memory build's bytes equal the file build's.
- **Memory per map (PC, measured):** 9.6 MB of tag cache, plus pixels and
  samples held in RAM: marines 61.7 MB (dialogue included), Covenant 30.8 MB,
  dropships 3.7 MB. Each takes about 1 s to build at a Command game's start.
  That RAM cannot fit on the Vita. There, pixels and samples will be read by
  offset from the source campaign map's existing decompressed cache copy
  (Halo CE's map cache; lead OK, Nov 5), and only the tags stay in memory.

## 5. The commander's view and controls (built: the camera step, Nov 5)

The commander's own unit stays hidden and unhurt where it stood, as Forge's
monitor leaves the Spartan. The camera looks down at a fixed tilt of 55
degrees at a **focus** point on the ground, 8 to 45 wu along its line. The
focus height follows the top of the ground under it (a base's roof, not its
floor), eased so hills don't jolt it. Where the level stands between focus
and camera, the camera comes in at once and eases back out, and it is held
above the ground under it, so it never sees from inside a hill or a roof.
On a pad the focus is the **cursor** (the screen centre). On PC the mouse
pointer is free over the view (ui_widget.c hands it to command.c while no
menu is up).

The world shows rings on the ground: one under every unit (the chosen
squads' bright green, the others' blue, the enemy's red), the cursor, the
growing circle while A is held, and the chosen squads' order points. Each
ring point sits on the ground under it.

| action | Vita | pad (Xbox / PlayStation) | PC |
|---|---|---|---|
| **in and out of first person**: into your own Spartan wherever it stands (it spawns at your base), and back up | Select + D-pad up | Back + D-pad up / Share + D-pad up (Back + RS click too) | Tab |
| pan (speed scales with zoom) | left stick (L: faster) | left stick (LT/L2: faster) | W A S D or the screen's edges (Shift: faster) |
| turn about the focus / zoom | right stick across / up-down | right stick | Q, E / wheel |
| choose the squad at the cursor; held: a growing circle chooses every squad in it | Cross | A / Cross | left click; left drag: a box |
| all your squads | D-pad right | LB / L1 | Ctrl + A |
| camera to your base; twice: to the last alert (a squad of yours hurt) | D-pad left | RB / R1 | B |
| order at the cursor: ground = MOVE (several squads spread on a ring), an enemy = ATTACK | R | RT / R2 | right click |
| hold where they stand | Square | X / Square | H |

The HUD's hint line and the Forge menu's CONTROLS panel name the buttons as
the player's button icons show them (HALO_BUTTON_ICONS: Xbox, PlayStation,
keyboard), with the in-and-out combo first.

## 6. Dropships (built Nov 5: the reinforcements step)

UNSC reinforcements come by **Pelican**: it flies to the side's landing
spot, hovers, drops the squad and leaves. With the vehicle bay upgrade it
carries a Warthog underneath, as in the CE campaign (the upgrades step). The
**Covenant dropship** drops Grunts, Jackals and Elites, or a Ghost for
vehicles (later). The flight is a **fixed path** from the setup's path
markers: the dropship object is moved along it by command.c (positions,
velocities and facing eased in and out each tick), not flown by the AI. Its
riders sit in its seats (the campaign's own seats) and get out at the hover,
the way the campaign's dropships unload. A dropship can be shot down: if it
is destroyed before the drop, its squad is lost. Its tags come from the
in-memory build out (section 4).

As built:
- **Which ship**: by the squad's species, for either team: a squad led by
  marines rides the Pelican, a Covenant squad the dropship. Red's Elite-led
  squad comes in a dropship, blue's marines in a Pelican.
- **When**: the game's first wave is made at the bases; every later wave (and
  every bought squad, from the economy step) flies in.
- **The path** (`struct command_path`, one per base, the markers' data):
  the landing spot 11 wu to the side of the base's rally point (flat ground,
  open sky: clear of the first squad and the commander's Spartan); in from
  50 wu behind the base (nearer where the map ends sooner), over the base, to
  12 wu over the landing spot, at a cruise height 10 wu over the highest
  ground under the way and under the level's sky. It is derived from the
  map's bases until the Command setup's Forge markers place it (section 3).
  Blood Gulch: red's ~100 wu, blue's ~80 wu.
- **The flight**: in along a Catmull-Rom curve through the points, slowing to
  a stop over the landing spot (15 wu/s average); straight down to the hover
  (3.5 wu/s), the lowest rider's feet 0.6 wu (Pelican) or 1.4 wu (dropship)
  over the ground; `vehicle_scripting_unload` (the campaign's
  `vehicle_unload`), again every half second for riders still in; 3 to 8 s
  of hover (any rider still in is put out at 8 s); up, turning for home; out
  the way it came, faster as it goes; deleted at the path's end. About 23 s
  in all. `command_tick` (a hook in game.c before the objects update, as a
  recording moves its unit) sets the position, facing and velocity each
  tick; `vehicle_hover` turns its own lift off, and players can't get in
  (`unit_set_enterable_by_player`).
- **Riders**: seated by `vehicle_scripting_load_magic` (the campaign's
  `vehicle_load_magic`) in the Pelican's "rider" seats or the dropship's
  "passenger" seats. A member that takes no seat is made under the hover as
  the others get out. Down, the squad walks 7 wu out from under its ship
  toward the field (MOVE, then HOLD there); blue's then ADVANCE. A squad in
  the air can't be chosen or ordered; the HUD lists it as inbound.
- **Shot down**: the campaign's ships have no vitality (their tags' maximum
  body and shield are 0: nothing hurts them), so command.c keeps a hull of
  its own, fed by a hook in `object_cause_damage` (damage.c) with the damage
  as the engine counts it before materials: Pelican 1200, dropship 1500. A
  rocket's blast on a ship is about 310, so 4 rockets bring a Pelican down
  and 5 a dropship. At its hull the ship dies as the game kills a unit
  (`unit_kill`: its own death and explosion); riders still in die with it,
  the squad is lost, and the HUD says so ("Squad 3 lost: its Pelican was
  shot down", "Enemy dropship shot down"). The wreck keeps its way, falls and
  rolls (command.c: the campaign's ships have no physics against the level,
  so it would fall through it) until a ray meets the ground, and is cleared
  20 s later.
- **Tests**: `command wave [red|blue] [kind]` sends a side's wave now (kind:
  the waves' table, from 0); `command shoot <flight> [hits]` puts the game's
  own rocket blast on a flight's ship; `command where` lists the flights.

## 7. Squads and orders on the existing AI (built)

A squad is command.c's own table: a side, up to 4 units, the order and its
point, and each member's place on a ring around that point (each place on
the ground under it, with its own walkable surface for the path search).
Each unit gets a free actor, the same way Forge places a character
(`actor_customize_unit`, `ai_scripting_attach_free`), on the side's team.
The actor is kept always active (`force_active`), because a dormant actor
is not updated and the order would never reach it.

No new AI: the orders steer the engine's actors the same way Halo 2's
command scripts do (`h2_ai_actor_orders`). One hook in actors.c,
`command_actor_orders`, runs after the action's own control and does
nothing unless Command mode runs. It calls `actor_move_to_point`, which
uses the AI's own path searches over the level's walkable surfaces.

- **MOVE**: each member goes to its place in the combat stance. In the
  asleep or alert stance the engine stops or slows the move, a relaxed walk
  of 0.35 wu/s; the combat stance runs at about 2 wu/s. Members fight as
  they go. When all are within 2.5 wu, the order becomes HOLD.
- **ATTACK**: the members magically see the target (as `ai_magically_see_unit`).
  A member with a target of its own fights it; otherwise it closes on the
  target's position, which is followed each frame.
- **HOLD**: the actors' own behaviour, but a member more than 6 wu from its
  place is walked back to it.
- **FOLLOW** (later): the point is a unit's position.

## 8. First person: the commander's Spartan, AI while he commands (built)

The owner's decisions of Nov 5 and 6: first person is only the commander's
own MP Spartan, and AI units are never played. The combo (Select + D-pad up,
or Tab) takes the commander into his Spartan wherever it stands, and back
above the map. **While he commands, his Spartan is AI-controlled** (owner,
Nov 6): it fights, takes cover and follows orders like a squad member.

As built:
- Leaving: the player moves into a placeholder body (an unseen, unhurt copy
  of the Spartan's own biped at his first spawn, ignored by the AI's
  actors: the game has no player without a unit), and the Spartan gets a
  free actor of a strong infantry variant, the campaign's armoured marine
  (`COMMAND_SPARTAN_ACTOR`), in the Spartan's own body, weapons and colours.
  It keeps the fight it was in: once its new actor is active it sees the
  enemies within 25 wu at once (as an attack order's target).
- Taking it back: the actor is deleted (`actor_delete`, which keeps the
  unit) and the player goes back into it (`players_set_local_player_unit`,
  the old dropping-in path reversed).
- The squad list's **"Spartan"** entry (a squad of one, `hero`): chosen and
  ordered as any squad (MOVE / ATTACK / HOLD); with no order it **defends
  the base**, holding 5 wu behind its rally point.
- Dead as AI, it is the player's death (`player_died`): the game respawns
  him at the base with the hero loadout (below), and above the map the new
  Spartan is the AI's again, defending the base.
- Every spawn gets the hero loadout of section 12b: the map's own assault
  rifle and magnum, 4 frag and 4 plasma grenades, and the game's own
  overshield (`object_double_charge_shield`). The paid respawn comes with
  the economy.
- A fix this needed in damage.c: an overshield drains each tick through the
  HUD's shield tick, which assumed a player's unit; a unit no player has now
  drains the same with no HUD tick.

### (removed) Dropping into a unit and leaving

`players_set_local_player_unit` (the old cheat_bump_possession path)
puts the player in the unit. Its actor **stays attached**: an actor gives
its unit no control while a player has it (actors.c). Leaving puts the
player back in the hidden commander unit, and the actor drives the unit
again on the next tick, with its squad's order. If the unit dies, the
player goes back above the map. One player per unit, and only the
commander's side. In network play (later) the host does the same through
`network_player_attach_unit`.

## 9. Multiplayer (later)

Network play uses the existing Forge networking (forge_net.inc, host
authoritative) and the AI sync (network_actors.c: only the host runs the
AI). New client-to-host requests: `order`, `possess` / `release`,
`upgrade`. The host sends the squads table, resources, territory owners and
scores, upgrade progress and dropship flights. The host checks every
request: the asker must be that side's commander, the squad and unit must
be his side's, the cost must be paid, and positions are clamped to the map.

## 10. AI commander (last)

When a commander seat is empty, an AI commander buys upgrades on a fixed
priority (barracks, then supply, then vehicle bay, then defence), sends
squads to the nearest supply point or territory it doesn't hold, and holds
what it has. It uses the same order code as a human commander.

## 11. Order of work, and what is proven

1. **Core (done on PC, Blood Gulch):** commander view, squads, orders,
   drop in and leave. Proven by harness runs; see the lane report.
2. Dropship drops: path markers, Pelican and Covenant dropship, shoot-down
   (done, Nov 5: section 6).
3. HQ upgrades and the economy: supply territories and the HQ trickle.
4. Objectives: territories and flag.
5. AI commander.
6. Map built out at load in memory (section 4). This replaces the prototype's
   Forge packs, and it is needed before step 2 ships.

Later: network play, the free mouse pointer, upgrade levels, more maps, the
Vita version, Halo 2 maps.

## 12. Vita budget

The engine and Forge cap is 32 AI alive (FORGE_MAXIMUM_AI). The owner's
Vita target is 30 fps with 15 bots, and actors cost more than bots.

| | PC | Vita (first) |
|---|---|---|
| AI alive, both sides (owner, Nov 6: 32 on both; each team 16, its Shade crews in it, the Spartan not; Flood and Sentinels later on top) | 32 | 32 |
| squads per side | 4 | 2-3 |
| units per infantry squad | 4 | 3 |
| vehicles per side | 3 | 2 |
| humans | 8 | 4 |

The caps are one table in command.c. A squad waits for room the way Forge's
characters do. The Vita numbers will be measured on the dev Vita after the
PC loop is right, not guessed, and the build-out's memory (section 4) is
part of that measurement.

## 12b. Later layers (owner, Nov 5; spec only, built after the AI commander)

**Bases' Shades (built in the game-type step).** Each base has 2 Shades,
Halo CE's own stationary plasma turret, from the player's own b40
(command_covenant's recipe). They sit on the base's roof edge before its flag
stand, facing the field. The spots come from the map's own bases (a ray down
finds the roof), not from coordinates we ship. Each is manned by a Grunt of
its own team. A destroyed Shade is rebuilt with the base's next wave, and
the defence upgrade adds more. Spartans use them as normal turrets.

**Hunters.** A Hunter pair is the heavy unit: the most expensive, slow to
arrive, unlocked by a base upgrade and never in the free waves. Cost tiers
for the economy step: marines and Grunts cheap; Jackals and Elites more;
the Hunter pair most.

| unit | cost (starting numbers) | arrives |
|---|---|---|
| marine squad (4) | 40 | next wave |
| Grunt squad (4) / Grunts + Jackals | 40 / 60 | next wave |
| Elite-led squad (1 + 3 Grunts) | 90 | next wave |
| Hunter pair | 220 | 60 s after the order (barracks upgrade) |
| the commander's Spartan's respawn (after a death; the first spawn free) | 90 (an Elite squad's) | when the team can pay; the HUD shows the cost and "can't afford" |

**The commander's Spartan (hero) loadout.** One table, so it's easy to
change. Every paid spawn uses it; base upgrades improve it.

| item | standard | with the armory upgrade |
|---|---|---|
| primary | assault rifle | assault rifle |
| secondary | pistol (magnum) | sniper rifle or rocket launcher (the commander's pick) |
| frag grenades | 4 | 4 |
| plasma grenades | 4 | 4 |
| overshield | yes, on each spawn | yes |

All are Halo CE's own items. The Spartan spawns at the team's base and
stays on the field while the commander commands, standing where it was
left. It can die, and it respawns at the base on a timer (and, with the
economy, for the cost above).

**Neutral enemies: the Flood and the Sentinels.** These are Halo CE's own
flood and sentinel teams, hostile to both teams, built in memory from the
player's 343 Guilty Spark / The Library. They get their own share of the AI
budget, so the teams never lose unit slots to them.
- **Sentinels** guard the supply points from the start: a few patrol each,
  and they must be cleared before the point can be taken.
- **Flood** come late only. Outbreaks start once the match is well along:
  after 10 minutes, or after 60 deaths in total, whichever comes first.
  They then grow: one outbreak every 90 s at first, then every 60 s, then
  every 45 s. Each outbreak is a pod of infection and combat forms at one of
  the map's marked outbreak spots, and it attacks whoever is nearest.
  Infection forms raise fallen marines and Elites as combat forms (CE's own
  behaviour), so a fight left alone feeds the Flood.
- **The Flood can force a draw.** If the Flood overrun both HQs (bases
  infested: Flood inside and no team unit for 20 s), or hold most of the
  territories for 90 s, the match ends in a draw. Before that, a warning
  plus a 60 s countdown ("The Flood are overrunning the map") gives both
  teams time to turn on the Flood. One base falling hurts that team badly
  but does not end the match; only both triggers the draw.

**Team colour.** After the AI commander, check per species which AI can
show team colour through CE's change-colour slots, and report before
changing anything. Elites and Grunts use those slots for rank colours;
marines, Jackals, Hunters, Flood and Sentinels are unknown. Until then,
team identity is the ground rings, plus small team-coloured markers over
friendly AI heads in first person (like MP teammate markers), which come
with the real HUD. Spartans show their team through their own MP colours.

## 13. Owner decisions (Nov 5)

1. HQ = each map's own bases, with upgrades in the base (no free build pads
   in v1).
2. Objective: territories and flag; a variant uses either or both.
3. An AI commander plays the seat when no human takes it.
4. Ground players give no orders.
5. Halo CE big maps and Halo CE units only; Blood Gulch only for now.
6. Name: "Command mode", "COMMAND" in the menu.
7. Reinforcements arrive by dropship on fixed paths; dropships can be shot
   down.
9. Teams are red and blue, each army a mix of marines and Covenant; species
   keep their campaign roles; both bases get 2 Shades manned by their own
   Grunts; Hunters are the heavy unit, unlocked by an upgrade.
10. First person is only the commander's own Spartan: it spawns at the base,
    stays where it was left, respawns at the base (paid after the first),
    with the hero loadout of 12b.
11. The Flood (late, growing, can force a draw) and Sentinels (guarding
    supply points) are neutral enemies, a layer after the AI commander.
8. The map is built out at load, in memory, from the player's own campaign
   maps; no built or converted files.
