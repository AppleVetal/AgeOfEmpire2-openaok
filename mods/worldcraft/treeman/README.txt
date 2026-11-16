[Russian version](README_ru.txt)

Walking Tree / WALKTREE

Custom images from outputs/walking-tree-unit, prepared in the
"Design a Walking Tree Animation" subproject.
walking-tree.drs contains only these images: SLP 59000–59004.
The original game archives and palette are not included in this package.
Rendering uses the palette from the user's copy of Age of Kings.

Five states: idle, walk, attack, death, and remains.
Eight directions; five are stored in the SLP, three are mirrored by the engine.
Each frame lasts 80 ms. The hit occurs on frame 12, after 960 ms.
The original package has no custom sounds, shadow, or player-color remapping.

unit.json defines the initial parameters; infantry combat upgrades apply
normally. Villagers of every civilization can grow treants from the start.
Rules and artwork are included in the network compatibility identifier.
English and Russian labels are in ../strings.en.json and ../strings.ru.json;
the installer saves the choice, with English used when no choice is saved.
Use --worldcraft-language ru to override it for one launch.

WALKTREE_WILD is a peaceful neutral form with the same artwork.
Every new random map starts with wild_count of these creatures.
A villager or fighter approaching within tame_radius permanently turns
the neutral treant into the player's WALKTREE combat unit.
Taming is free and increases population by population, even above the limit;
claiming it again by approaching is disabled. Growing remains available.
Existing scenarios retain their own unit composition.
