# Steam library: DRM-free games for this device

Owned Steam games that PCGamingWiki lists as DRM-free on Steam (runnable without the Steam
client), tiered by how likely they are to run on this Atom x7-Z8700 / HD Graphics. Generated
2026-10-07 from the Steam Web API (1353 owned games) + PCGamingWiki page wikitext (991 pages
matched by Steam AppID; 362 games had no confirmed page, so this list is incomplete).

- Tiers are an estimate, not benchmarks. "DRM-free" is PCGamingWiki's claim: verify by running.
- Linux-native vs Windows doesn't matter (Wine is installed); `os` is from the Steam store.
- Controller = PCGamingWiki says it has controller support (full vs partial not known).
- Download: `depotdownloader -app APPID [-os linux|windows] -username NAME -remember-password -dir ~/Games/steam/<name>`
  (the user types their password / Steam Guard code; don't handle Steam credentials).
- Games that exit with "SteamAPI_Init() failed" (PCGamingWiki still said DRM-free, e.g. Unepic 233980):
  they only need a Steam API, not the client. Fix: replace the game's `libsteam_api.so` (64-bit: `lib64/`,
  32-bit: `lib32/`) with the gbe_fork emulator (github.com/Detanup01/gbe_fork, release
  `emu-linux-release.tar.bz2`, `regular/x64|x86/libsteam_api.so`; unpacked in `~/Games/tools/gbe`, not in
  the repo), keep `libsteam_api.so.orig`, add `steam_settings/steam_appid.txt` with the app id. Native
  Linux builds only: Steamless is for SteamStub-wrapped Windows exes, not needed here. Unepic also
  needed `gpd-launch.sh` edited to `exec ./unepic64s` (its `unepic.sh` wanted a missing `unepic64steam`).
  Unepic then crashed after the loading screen: the game checks `SteamFriends()` persona-name history
  against a blocklist and gbe returns a junk pointer (0x64) into `strcmp`. Fix: `31 c0 c3`
  (`xor eax,eax; ret`) at file offset 0xe0b60 of `unepic64s` (original: `unepic64s.orig`); also put the
  game's interface list in `steam_settings/steam_interfaces.txt` (`tools/generate_interfaces` on the
  `.orig` lib). Found with gdb: break on the fault, read the stack/registers, disassemble the caller.

## Very likely runs well, controller support

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| Super Meat Boy | 40800 | linux | 6.0 |
| VVVVVV | 70300 | linux | 0.0 |
| Gish | 9500 | windows | 2.5 |
| They Bleed Pixels | 211260 | linux | 0.0 |
| SteamWorld Dig | 252410 | linux | 0.0 |
| Salt and Sanctuary | 283640 | linux | 1.9 |
| Blasphemous | 774361 | linux | 0.5 |
| Hyper Light Drifter | 257850 | linux | 0.0 |
| Titan Souls | 297130 | windows | 0.0 |
| Broforce | 274190 | linux | 0.0 |
| Hotline Miami | 219150 | linux | 0.2 |
| Hotline Miami 2: Wrong Number | 274170 | linux | 0.0 |
| Risk of Rain (2013) | 248820 | linux | 0.2 |
| Monaco | 113020 | linux | 0.1 |
| Party Hard | 356570 | linux | 0.0 |
| Hue | 383270 | linux | 0.0 |
| Splasher | 446840 | linux | 0.0 |
| Cave Story+ | 200900 | windows | 0.0 |
| Bastion | 107100 | linux | 9.1 |
| Transistor | 237930 | linux | 0.6 |
| Undertale | 391540 | linux | 0.0 |
| Stardew Valley | 413150 | linux | 2.1 |
| Darkest Dungeon® | 262060 | linux | 0.0 |
| SteamWorld Heist | 322190 | linux | 0.1 |
| Death Road to Canada | 252610 | linux | 0.0 |
| Moon Hunters | 320040 | linux | 0.0 |
| Hammerwatch | 239070 | linux | 0.3 |
| Atom Zombie Smasher | 55040 | linux | 3.1 |
| Cthulhu Saves the World | 107310 | windows | 0.5 |
| Breath of Death VII | 107300 | windows | 0.0 |
| A Bird Story | 327410 | linux | 0.0 |
| Beholder | 475550 | linux | 0.0 |
| Fran Bow | 362680 | linux | 0.0 |
| Kingdom: Classic | 368230 | linux | 0.0 |
| Kingdom: New Lands | 496300 | linux | 0.2 |
| Day of the Tentacle Remastered | 388210 | linux | 0.0 |
| Full Throttle Remastered | 228360 | linux | 0.0 |
| Grim Fandango Remastered | 316790 | linux | 1.8 |
| Psychonauts | 3830 | linux | 3.6 |
| Quake | 2310 | windows | 7.4 |
| Heretic: Shadow of the Serpent Riders | 2390 | windows | 0.0 |
| Hexen: Beyond Heretic | 2360 | windows | 0.0 |
| Heretic + Hexen | 3286930 | windows | 0.3 |
| Wolfenstein 3D | 2270 | windows | 0.0 |
| Strife: Veteran Edition | 317040 | linux | 0.5 |
| Serious Sam Classic: The First Encounter | 41050 | windows | 0.0 |
| Serious Sam Classic: The Second Encounter | 41060 | windows | 0.0 |
| SiN Gold | 1313 | windows | 0.6 |
| Thief Gold | 211600 | windows | 0.0 |
| Thief™ II: The Metal Age | 211740 | windows | 0.0 |
| Red Faction II | 20550 | windows | 0.0 |
| Mafia | 40990 | windows | 0.0 |

## Maybe: test at low settings

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| Hollow Knight | 367520 | linux | 0.0 |
| A Short Hike | 1055540 | linux | 0.0 |
| Pyre | 462770 | linux | 0.0 |
| Rayman Origins | 207490 | windows | 0.0 |
| Kentucky Route Zero | 231200 | linux | 3.2 |
| Half-Life 2 | 220 | linux | 25.1 |
| Half-Life: Source | 280 | linux | 0.0 |
| Portal | 400 | linux | 5.4 |
| Prince of Persia: The Two Thrones | 13530 | windows | 0.0 |
| Hitman: Contracts | 247430 | windows | 0.0 |
| Amnesia: The Dark Descent | 57300 | linux | 0.5 |
| GRIS | 683320 | windows | 0.0 |
| The Swapper | 231160 | windows | 0.0 |
| Disco Elysium | 632470 | windows | 0.5 |
| Valkyria Chronicles™ | 294860 | windows | 0.0 |

## Light, but mouse/keyboard driven (GPD mouse mode or thumb keyboard)

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| FTL: Faster Than Light | 212680 | linux | 0.9 |
| Papers, Please | 239030 | linux | 0.0 |
| Baldur's Gate: Enhanced Edition | 228280 | linux | 50.8 |
| Baldur's Gate II: Enhanced Edition | 257350 | linux | 55.6 |
| Legend of Grimrock | 207170 | linux | 0.0 |
| The Blackwell Legacy | 80330 | linux | 0.0 |
| Blackwell Unbound | 80340 | linux | 0.0 |
| Blackwell Convergence | 80350 | linux | 0.0 |
| Blackwell Deception | 80360 | linux | 0.0 |
| Primordia | 227000 | linux | 0.0 |
| Ben There, Dan That! | 37420 | windows | 0.6 |
| Time Gentlemen, Please! | 37400 | windows | 0.0 |
| Fallout | 38400 | windows | 0.0 |
| X-COM: UFO Defense | 7760 | windows | 0.1 |
| X-COM: Terror from the Deep | 7650 | windows | 0.0 |
| Deus Ex: Game of the Year Edition | 6910 | windows | 22.3 |
| Arx Fatalis | 1700 | windows | 0.0 |
| Dungeon Siege | 39190 | windows | 2.3 |
| The Binding of Isaac | 113200 | windows | 0.7 |
| Crayon Physics Deluxe | 26900 | windows | 0.0 |
| Samorost 2 | 40720 | windows | 0.0 |
| Space Rangers HD: A War Apart | 214730 | windows | 5.0 |
| Roadwarden | 1155970 | linux | 0.0 |
| Nihilumbra | 252670 | linux | 0.0 |
| Stronghold HD | 40950 | windows | 0.0 |

## Probably too heavy, or not judged

| Game | AppID | Native build | Hours played |
|---|---|---|---|
| Age of Wonders III | 226840 | linux | 2.7 |
| AI War: Fleet Command | 40400 | linux | 1.5 |
| Alpha Protocol | 34010 | windows | 20.7 |
| Amnesia: A Machine for Pigs | 239200 | linux | 2.0 |
| Among the Sleep | 250620 | linux | 0.0 |
| Antichamber | 219890 | linux | 0.0 |
| ARK: Survival Evolved | 346110 | windows | 0.0 |
| Arma: Cold War Assault Remastered | 65790 | linux | 0.2 |
| Assassin's Creed | 15100 | windows | 6.5 |
| ATOM RPG Trudograd | 1139940 | linux | 0.0 |
| Audiosurf | 12900 | windows | 0.0 |
| Baldur's Gate 3 | 1086940 | windows | 4.1 |
| Balrum | 424250 | linux | 0.3 |
| Barotrauma | 602960 | linux | 0.0 |
| Batman™: Arkham Knight | 208650 | windows | 0.0 |
| Battlefield: Bad Company™ 2 | 24960 | windows | 1.1 |
| Beat Cop | 461950 | linux | 0.0 |
| Besiege | 346010 | linux | 0.0 |
| Bionic Dues | 238910 | linux | 0.2 |
| Bloodstained: Ritual of the Night | 692850 | windows | 0.0 |
| Breathedge | 738520 | windows | 0.0 |
| Brütal Legend | 225260 | linux | 0.0 |
| Call of Juarez | 3020 | windows | 0.0 |
| Caves of Qud | 333640 | linux | 0.4 |
| Command & Conquer™: Renegade | 2229890 | windows | 0.4 |
| Cortex Command | 209670 | windows | 0.0 |
| Cruelty Squad | 1388770 | windows | 0.0 |
| Crusader Kings II | 203770 | linux | 1.4 |
| Crysis | 17300 | windows | 7.8 |
| Crysis Warhead | 17330 | windows | 8.2 |
| Darksiders Genesis | 710920 | windows | 0.0 |
| Darksiders II Deathinitive Edition | 388410 | windows | 1.2 |
| Darkwood | 274520 | linux | 0.0 |
| Divinity II: Developer's Cut | 219780 | windows | 0.0 |
| Divinity: Original Sin (Classic) | 230230 | windows | 1.6 |
| Divinity: Original Sin 2 | 435150 | windows | 1.4 |
| Dungeon Siege 2 | 39200 | windows | 0.2 |
| Eldritch | 252630 | linux | 0.5 |
| Europa Universalis IV | 236850 | linux | 0.0 |
| Exanima | 362490 | windows | 0.3 |
| Expeditions: Viking | 445190 | windows | 0.0 |
| F.E.A.R.: Perseus Mandate | 21120 | windows | 0.0 |
| Far Cry 2 | 19900 | windows | 46.6 |
| Firewatch | 383870 | linux | 5.8 |
| Galactic Civilizations II: Ultimate Edition | 202200 | windows | 0.0 |
| Gorky 17 | 253920 | linux | 0.0 |
| Grand Theft Auto: San Andreas | 12120 | windows | 0.8 |
| Hades | 1145360 | windows | 0.5 |
| In Sound Mind | 1119980 | windows | 0.0 |
| Kerbal Space Program | 220200 | linux | 0.1 |
| Noita | 881100 | windows | 0.1 |
| Nosferatu: The Wrath of Malachi | 283290 | windows | 0.0 |
| Obduction | 306760 | windows | 0.0 |
| Orcs Must Die! 3 | 1522820 | windows | 0.0 |
| Ori and the Blind Forest | 261570 | windows | 0.0 |
| Ori and the Blind Forest: Definitive Edition | 387290 | windows | 3.6 |
| Ori and the Will of the Wisps | 1057090 | windows | 0.0 |
| Outlast | 238320 | linux | 0.0 |
| Overgrowth | 25000 | linux | 0.0 |
| Painkiller: Redemption | 65560 | windows | 0.0 |
| Pathologic Classic HD | 384110 | windows | 0.0 |
| Phantom Fury | 1733240 | windows | 0.0 |
| Pillars of Eternity | 291650 | linux | 37.1 |
| Pillars of Eternity II: Deadfire | 560130 | linux | 0.0 |
| PixelJunk™ Eden | 105800 | windows | 0.0 |
| Planetary Annihilation | 233250 | windows | 0.0 |
| Poly Bridge | 367450 | linux | 0.0 |
| Psychonauts 2 | 607080 | linux | 0.0 |
| Quake 4 | 2210 | windows | 0.0 |
| Rain World | 312520 | windows | 0.0 |
| Return of the Obra Dinn | 653530 | windows | 0.4 |
| Rise of the Triad: Ludicrous Edition | 1421490 | windows | 0.2 |
| Scanner Sombre | 475190 | windows | 0.0 |
| Shadow Tactics: Blades of the Shogun | 418240 | linux | 0.0 |
| Shadowrun Returns | 234650 | linux | 12.7 |
| Shadowrun: Dragonfall - Director's Cut | 300550 | linux | 20.1 |
| Sir, You Are Being Hunted | 242880 | linux | 0.4 |
| Sniper Elite | 3700 | windows | 0.0 |
| SOMA | 282140 | linux | 18.4 |
| Stellaris | 281990 | linux | 0.0 |
| Stories: The Path of Destinies | 439190 | windows | 0.0 |
| Styx: Master of Shadows | 242640 | windows | 9.6 |
| Styx: Shards of Darkness | 355790 | windows | 0.0 |
| Subnautica | 264710 | windows | 0.0 |
| Sunless Sea | 304650 | linux | 1.7 |
| System Shock 2: 25th Anniversary Remaster | 866570 | windows | 1.0 |
| System Shock: Enhanced Edition | 410710 | windows | 2.5 |
| Terminator: Resistance | 954740 | windows | 3.6 |
| The Ascent | 979690 | windows | 0.0 |
| The Book of Unwritten Tales | 215160 | linux | 0.2 |
| The Cave | 221810 | linux | 0.2 |
| The Fall | 290770 | linux | 0.0 |
| The Last Campfire | 990630 | windows | 0.0 |
| The Silent Age | 352520 | windows | 0.0 |
| The Vanishing of Ethan Carter Redux | 400430 | windows | 6.2 |
| The Void | 37000 | windows | 0.0 |
| The Witness | 210970 | windows | 6.6 |
| Thirty Flights of Loving | 214700 | linux | 0.0 |
| Tower of Time | 617480 | linux | 0.0 |
| Tyranny | 362960 | linux | 0.0 |
| Underworld Ascendant | 692840 | linux | 0.0 |
| Valley | 378610 | linux | 0.0 |
| Wasteland 2 | 240760 | linux | 0.0 |
| Wasteland 3 | 719040 | linux | 0.0 |
