[Russian version](README_ru.txt)

Worldcraft 0.4.0 — villagers create forests, mountains, treants, flowing rivers, and water moats

Controls
  Select one or more villagers. In the second panel row:
  three trees in a row — line; a square of trees — rectangular area;
  the cross on the right — cancel unfinished work in selected villagers' queues.
  Hold the left mouse button on the map, drag, and release.
  A single click places one tree. The preview shows tiles;
  red marks tiles that do not meet planting conditions.
  Shift queues the work and keeps placement mode active.
  Esc exits placement mode without canceling existing orders.
  Placing an order again on your own unfinished site adds workers.
  A selected unfinished site or growing tree can be removed with Delete.
  A fully grown tree becomes a shared resource and is chopped with an ordinary order.

Rules
  rules.json: wood costs, work time for one villager, and growth time.
  Initial values: 10 wood, 10000 ms of work, 60000 ms of growth.
  Each additional worker adds one third of a single villager's speed.
  Areas are processed from the center outward; lines from start to end.
  Up to 256 tiles per order, 256 unfinished/growing sites per player,
  and 256 sites in one villager's queue.
  Occupied, invisible, and unsuitable tiles are skipped. If wood is insufficient,
  only as many plantings as can be afforded are paid for. Working again on the
  same site does not charge again. Payment occurs when the order is issued.
  Future sites are reserved against construction but remain traversable.
  Active planting sites and growing trees block their tile. Units on the tile
  delay the start; planting does not move or destroy them.
  Cancellation returns the whole unused fraction of the cost; completed work
  and finished planting are not refunded. If no workers remain, unfinished
  work is canceled. Growing trees do not require workers.
  A normal new order, Stop, or combat removes a villager from its planting queue.
  A site the villager cannot reach after a bounded number of attempts is skipped;
  the queue continues. If nobody else is working there, payment is refunded
  under the same rules.

Walking Tree
  Select a villager and the first button in the third row — "Grow a treant".
  A click sets one site; dragging places a row. Shift queues tasks.
  Initial parameters: 600 hit points, 30 melee damage, armor 2/4,
  speed 0.6 tiles/s, line of sight 6. Each unit requires 200 wood, 50 gold,
  60 seconds of work for one villager, and 3 population slots. Multiple villagers
  speed up growing under the shared-work rules.
  If population space is insufficient, completed growing waits for a house.
  The site is occupied while the villager works; the completed treant frees
  the tile and receives normal movement, attack, stop, and formation commands.
  A treant is not a wood resource. Canceling before completion refunds the unused
  fraction of wood and gold. Common infantry combat upgrades apply to it;
  growing costs are defined separately in the mod settings.
  Each new random map places 10 peaceful wild treants on land away from starting bases.
  They wander slowly and provide no resources when gathered.
  Bring your villager or fighter within 3 tiles: the nearest unit tames
  the treant for free. Equal distances use a stable unit order that is the same
  for every network participant.
  Once tamed, it is a normal fighter; enemy units cannot claim it by approaching.
  It uses 3 population slots even above the limit.
  Count and distances are set by wild_count, wild_start_distance, and tame_radius
  in treeman/unit.json. Treants are not added to existing scenarios.
  Rules are in treeman/unit.json; artwork is in a separate walking-tree.drs.
  Text is in strings.en.json and strings.ru.json.
  For multiplayer, participants need matching builds and the full Worldcraft package.
  This branch uses simulation revision 6 and save format 16;
  saves and replays from the previous revision are incompatible.

Mountains, dry moats, ditches, lakes, and terrain restoration
  The fourth button in the second row opens the earthworks panel.
  The first three buttons choose height: hill (4), mountain (12), tall mountain (18).
  Mountain is selected by default, roughly as tall as a tower or castle.
  Next are line/area pairs: mountain, dry moat, restoration, ditch, lake.
  The last two buttons cancel unfinished work and return to the normal panel.
  Each selected mountain tile automatically gets slopes up to six tiles wide;
  they are included in the cost. An area creates a shared summit.
  Maximum height is 24. Repeat the order to increase height.
  A difference of up to one level is traversable; steeper raised tiles block movement.
  A dry moat lowers the ground by two levels, down to a minimum of -4, and blocks
  passage. A shallow one-level ditch remains traversable. A lake creates
  water at elevation -2 with water passability. Restoration returns the tile's
  original elevation and terrain type, including original water.
  Cost and time per tile level: mountain — 10 wood and 15 seconds;
  dry moat, lake, and restoration — 5 wood and 10 seconds; ditch — 3 wood
  and 5 seconds. For a mountain, raising the selected tile pays for its slopes too.
  Settings are in rules.json.
  Queueing, shared work, payment, and cancellation follow the forest-planting rules.
  The surface to be changed must be explored. Workers approach from outside
  the site. Before completion, creatures, trees, and other resources move
  to suitable free tiles outside the planned work.
  Entities, health, ownership, and remaining resources are preserved.
  If any object has no space within 32 tiles, work waits; there is no partial
  relocation. Water under a mountain or dry excavation is replaced by sand.
  Buildings and their foundations do not move or change. A mountain can be built
  right next to them; tiles under buildings are excluded from slopes. A new
  foundation or planting cannot occupy an already reserved work site.
  Inaccessible tiles are skipped. Unfinished earthworks do not block passage.
  Unlike trees, earthworks have no separate growth period.
  A mound or pit marks the work site; the finished mountain or moat changes
  the map's actual terrain. Canceling does not remove completed mountains:
  use restoration. Hidden elevation and terrain changes appear only after exploration.

Data
  manifest.json — package identifier, version, schema, and filenames.
  graphics.json — references to existing original-game sprites:
  a stump for a tree planting site, a separate tree during growth, and an oak afterward.
  Earthworks use the engine's own mounds and pits.
  This package does not yet load new external SLP files.
  strings.en.json and strings.ru.json — English and Russian names, commands,
  tooltips, and notifications for all Worldcraft additions.
  Original extracted files are not modified. The package contains its own
  settings and references to data from the user's installed game.
  Restart the game after changing settings. Network participants must have
  matching engines and package files, including both language dictionaries.
  Both dictionaries enter compatibility checks regardless of the selected language.

Interface language
  The installer saves your choice; otherwise English is the default.
  To override it for one launch, run from the build directory:
    OpenAoK.exe --worldcraft-language en
    OpenAoK.exe --worldcraft-language ru
  The option affects only Worldcraft additions: planting, earthworks, rivers,
  treants, the Trojan Horse, and Gorynych. Original-game text keeps the language
  of the user's game data. Missing or unsupported language arguments are errors.
  Changing this option does not change game rules or the content fingerprint.
  Updating the localization package itself changes that fingerprint; older
  saves, replays, or clients with different package files remain incompatible.

Rivers and water moats
  The villager's second button in the third row opens waterworks.
  The top row sets line width: 1, 3, or 5 tiles (default 3).
  Second row: river line, river area, water moat line, moat area.
  Bottom row: restoration line, restoration area, cancel, and back.
  Lines expand to both sides with square ends. Diagonal tiles connect along edges.
  Area size is the dragged rectangle; selected width has no effect on it.
  The 256 limit includes the full width; an order exceeding it is rejected entirely.
  Villagers work from the channel center toward its banks. A completed tile
  immediately gets water at level -2; no water source is needed. Flow direction
  runs from the start of the drag to its end. For a single tile, flow follows
  the map's positive X axis. Flow is visual only and does not change ship speed.
  Water moats have no flow.
  Initial cost per elevation level changed: 5 wood and 10 seconds of work.
  On flat ground, a two-level-deep tile costs 10 wood and 20 seconds of work.
  Rivers block land units; ships pass if the channel is wide enough and banks are clear.
  Water moats also block ships.
  Obstacles move under the shared earthworks rules; buildings are preserved.
  Their tiles are skipped, so a channel near a building may have gaps.
  Restoration returns the original elevation and terrain type and removes flow.
  To fill a wide body of water, start at an accessible bank; inaccessible
  sites follow the shared queue rules.
  Saves, replays, and networking carry order width and flow direction.
  Changes behind fog of war remain hidden until the site is explored again.

Formats
  Simulation revision 10, network protocol 9, saves 18, replays AOKCREC7.
  Saves and replays from the previous build are not automatically migrated.

Gorynych
  Each new random map starts with 10 peaceful neutral Gorynych dragons.
  Bring a villager or fighter within 3 tiles: Gorynych permanently joins
  the player and uses 5 population slots. Approaching cannot claim it again.
  A villager raises Gorynych with the dragon button in the bottom command row:
  400 wood, 300 gold, 120 seconds of work, 5 free population slots.
  Click for one Gorynych, drag for a row, and use Shift to add to the queue.
  If population space is insufficient, growing waits for free housing.
  Gorynych has 900 hit points, speed 1.6 tiles/s, and fire range 5 tiles.
  It flies over water, trees, mountains, and buildings. The three heads fire
  at one target only while hovering: 12 pierce damage before armor per head,
  a 3-second reload, and splash damage to nearby enemies within 0.6 tiles.
  Ordinary melee attacks cannot reach Gorynych; ranged attacks can hit it.
  Ships, wagons, and buildings do not accept it inside. Flying units do not
  occupy ground tiles for construction or earthworks.
  Animations, parameters, and source atlas: mods/worldcraft/gorynych.
  For network play, everyone needs matching builds and mod files.

Future features
  Other planned creatures have not yet been implemented. Lakes, rivers, and
  water moats do not simulate hydrodynamics: water occupies specified tiles at a fixed level.

Installed language selection
  The Windows installer saves the chosen English/Russian language for every launch.
  No extra launch option is needed. --worldcraft-language en|ru overrides the
  saved choice for one run; without a saved choice, English is used.
