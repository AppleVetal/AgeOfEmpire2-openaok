[Russian version](README_ru.txt)

Trojan Horse — land transport

Construction: select a villager, press the third button in the third command
row, and choose a free land tile. Drag to place a line of wagons; Shift
adds work to the queue. Several villagers can work together.
Cost per wagon: 250 wood and 100 gold; 60 seconds of work for one villager.
Completion requires 2 free population slots. Cancellation refunds unused
resources according to the shared construction rules.

400 hit points, speed 1.6 tiles/s, 12 seats. Cannot attack without shooters.
Passengers are not healed.
Your villagers, infantry, archers, and other foot soldiers can board.
Cavalry, siege engines, treants, monks, animals, and other wagons cannot.
Passengers continue to use population. All die if the wagon is destroyed.

Boarding: select passengers and right-click your wagon.
Unloading: select the wagon, press "Unload", then choose a location on land.
The wagon travels there and releases passengers onto nearby free ground.
If no free exit is available, passengers stay inside; clear space and repeat
the unload order. Rivers and water moats are avoided using land routes.
Repairing a damaged wagon requires an explicit repair order to a villager.

Shooting: archers, crossbowmen, and skirmishers use their own projectiles,
damage, and range, including upgrades. Spearmen and pikemen throw spears
up to 4 tiles: normal weapon damage becomes pierce damage; bonuses against
cavalry, elephants, and other classes are not carried over. After unloading,
fighters retain their normal weapons. Villagers and swordsmen add no shots
to the volley.
The shared reload time is the longest reload time among the shooters inside.
Each eligible passenger fires one projectile if the target is within range.
Fire is possible only while stationary, automatically or on an attack order.
The initial stance is stand ground; an attack order makes the horse move
into firing range. Unloading stops the attack.
Projectiles already fired retain their damage after the shooter unloads or dies.

Parameters: unit.json. English and Russian labels: ../strings.en.json and
../strings.ru.json. The installer saves the choice; otherwise English is used.
Use --worldcraft-language ru to override it for one launch.
Native SLP/DRS: trojan-horse.drs, graphics.json, animations.json.
Artwork was created with the built-in ImageGen: 5 directions mirrored to 8,
idle, 4 movement frames, and wreckage.
The transition to wreckage is immediate; there is no separate falling animation.
The source atlas, exact prompt, and converters are kept in the neighboring
outputs/trojan-horse-unit working folder. Original AoK assets are not included.

For network play, everyone must use the same build and identical mod files.
Simulation revision 9; saves and replays from 0.4.0 (revision 7)
cannot be loaded in this branch. The 0.4.0 installer does not contain the transport.
