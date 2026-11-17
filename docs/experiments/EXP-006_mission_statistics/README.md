# EXP-006 — Mission statistics reports

Date: 2026-11-16. Status: preliminary research; causes and fixes OPEN.

## Goal and hypotheses

Investigate the reported Shogun Showdown Hayamoto enemy-kill count and Hidden Valley
truck/ninja alarm, including whether a 1.01 correction can preserve that version's
content. Hypotheses: target classification or mission data may affect the first;
collision, death attribution or subsequent body discovery may affect the second.
None is established as the cause.

## Evidence and procedure

- The user supplied a GitHub suggestion reporting both behaviors and saying version
  1.02 fixes Hayamoto's count. This is a report, not a reproduction or proof of a
  specific version change. It also reports censorship in 1.02; not assessed here.
- A historical player walkthrough reports Hayamoto counting as an enemy:
  https://www.supercheats.com/pc/walkthroughs/hitman2silentassasin-walkthrough02.txt
  Its exact game build and conditions have not been established.
- A player walkthrough describes a truck running over a ninja:
  https://portforward.com/games/walkthroughs/Hitman-2-Silent-Assassin/8-Hidden-Valley.htm
  This is an independent report, not proof of which internal counter changes.
- CONFIRMED: The documented Steam executable is 1.0.0.277, SHA-256
  f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136;
  the previously analyzed retail executable is 1.0.0.265, SHA-256
  0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560.
  Neither establishes behavior of an identified 1.01 build.
- Verified the Steam snapshot hash with Get-FileHash. Opened the existing
  Steam read-only capture samples/local/dumps/steam-001/main.analysis.img in
  REA (SHA-256 f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a).
  Searches for EnemiesKilled, enemies killed, Statistics, alarm, and ZHM5 did
  not complete within the observation window and were cancelled. No negative
  result or code location is implied.
- CONFIRMED: Existing documentation identifies scene archives as ZIPs and GMS
  members as compressed data; their mission logic and format remain open.
  See [FILE_FORMATS](../../FILE_FORMATS.md).

## Level-asset inspection (2026-11-16)

- CONFIRMED: copied the C3 main, load and briefing ZIPs to
  `samples/local/mission-investigation-20261116/` and compared each SHA-256 with
  its source. The standard snapshot tool only copies root-level binaries/configs,
  so it does not include the scene archives. The install was not modified.
- CONFIRMED: `C3-2a__MAIN.ZIP` SHA-256
  `5e3676e996283886ad306dba09267947c198a81af7b3853d0056b1768787a5b1`
  contains `SCENES/C3-2a/C3-2a__MAIN.GMS`. After inflating the member as
  described in FILE_FORMATS, its data contains four occurrences of
  `Scripts\C3\C3_2\CheckPointNinja.sdl`, the adjacent declaration
  `ZGEOMREF examine;ZGEOMREF examine2;ZGEOMREF end;LONG id;`, and five
  occurrences of `Scripts\C3\C3_2\Van32a.sdl`. Adjacent to the latter are
  declarations `rPath1` through `rPath10`, `rCheckPoint`, `rList` and
  `startPath`. Other strings include `VehicleTempStop`, `VehicleContinue`,
  `SE_VehicleHit` and `TruckCol`. These establish names and serialized
  declarations, not their run-time meaning or links to specific objects.
  INFERRED: this is the Hidden Valley tunnel scene, based on the checkpoint
  ninja, truck, tunnel-specific script names and mission context. This is
  stronger than using the LOC text alone, which is shared across scenes.
- CONFIRMED: `C3-3__MAIN.ZIP` SHA-256
  `9fab51e790487c0f510a6549ca12a64425c0804e2d431038563f8cc92b86d226`
  contains `SCENES/C3-3/C3-3__MAIN.GMS`. Its inflated data has the strings
  `Icon_Target`, `Hayamoto`, and `Scripts\C3\C3_3\Hayamoto.sdl` at offsets
  0x54c33, 0x54cf0 and 0x54d59 respectively; a declaration immediately after
  the script name contains references to guards, guidance system and escape
  waypoints. An `Icon_Enemy` string appears near a ninja script at 0x4ed83.
  INFERRED: the Hayamoto strings are a serialized character record in the
  Shogun Showdown scene. An icon name does not establish kill classification.
- CONFIRMED: Neither main ZIP has `.sdl` members in its central directory, and
  no loose `.sdl` files appeared in an `rg --files` search of the Steam install.
  The names and declarations do not reveal whether compiled script logic lives
  elsewhere inside these assets or in the engine. No SDL execution or code has
  been reconstructed.
- Reproduction: open the listed ZIPs using Python's standard `zipfile`, read
  the GMS member, call `zlib.decompress(member[9:], -15)`, and search the
  resulting bytes for the quoted ASCII strings. Offsets are into inflated GMS,
  not offsets into the ZIP or executable. Archive members and sizes were
  inventoried using `ZipFile.infolist()`.

These findings narrow the investigation to mission-specific actors and their
references, but do not distinguish a bad path or checkpoint script from a shared
collision, body-discovery, kill-accounting or rating rule. Comparing an actual
1.01 scene and executable with the identified Steam versions remains necessary.

## Shared statistics and ranking (2026-11-16)

- CONFIRMED: the inflated GMS members of both C3-2a and C3-3 contain the
  class-name strings `ZWINDOW_StatisticsLevel` and `ZWINDOW_StatisticsOverall`,
  plus common UI paths including `AllLevels/Statistics/Alerts`,
  `AllLevels/Statistics/EnemiesKilled` and `AllLevels/Statistics/Rating`.
  This establishes shared names, not identical per-level rules or data.
- CONFIRMED (static): the Steam capture SHA-256
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`
  contains `z:\code\hitman2\gui\statisticsscreen.cpp` at VA 0x6914d8 and
  `AllLevels/Statistics/Ranking` at VA 0x691540. REA xrefs to the former:
  `ev_e74d5d1a2ca32f6b0aab274b5b5829df4e30cfa5ce9958cdb69e6af03a3e4651`.
  The PE section mapping sets file offset equal to RVA in this capture.
- CONFIRMED (static): Ghidra function `FUN_0055c8a0` (VA 0x55c8a0) reads a
  group of global values, combines several with different weights, applies
  floating-point math and writes two object fields. For example, VA 0x55c8fd
  bytes `a1 20 34 6b 00` read a global and VA 0x55c95b bytes
  `89 86 bc 00 00 00` write the first calculated value. In the same capture,
  code starting VA 0x55c21a reads the two fields, categorizes each by numeric
  thresholds, selects a table element, and at VA 0x55c2e6 pushes the Ranking
  string (`68 40 15 69 00`). This is evidence of shared executable-side
  ranking-related processing, not proof of which fields represent alerts,
  kills, stealth or aggression. Evidence:
  `ev_4396c95ee9cd56e0b25d1b5ecb719ce091122209e7f2245d2b923d4d1e249c13`;
  bounded disassembly: `llvm-objdump -d -M intel --start-address=0x55c8a0
  --stop-address=0x55c9d5 samples/local/dumps/steam-001/main.analysis.img`
  and the ranges 0x55c21a..0x55c2eb and 0x55c6e1..0x55c77a.
- CONFIRMED (static): VA 0x55c703..0x55c76d copies fields from a supplied
  structure into the global block used by `FUN_0055c8a0`; VA 0x55c775 calls
  that function. The event that builds this structure and meanings of its
  fields remain OPEN. `FUN_0055c8a0` has additional branch behavior that is
  outside the calculation described here. No runtime observation was made.
- INFERRED: mission-specific events and shared statistics processing can both
  contribute to the observed result. No level-specific exception has been
  identified; the 1.01 build has not been examined. Next trace the producer of
  the incoming structure and map each field using controlled mission runs;
  do not patch displayed numbers without checking the ranking and saved data.

## Malaysia objects and zero-stat challenge (2026-11-16)

- User reports that Hidden Valley ninjas can be killed by trucks and that the
  kill is attributed to the player. User also describes enemy-fire workarounds
  for breaking the Basement Killing computer and Graveyard Shift window while
  attempting all-zero statistics. These are reports; not reproduced here.
- The shipped official strategy guide recommends shooting the security computer:
  https://images10.newegg.com/UploadFilesForNewegg/itemintelligence/AMD/Hitman_202_20Silent_20Assassin_20__20Official_20Strategy_20Guide1519972734403.pdf
  A player-written Silent Assassin guide describes a necessary shot in that
  mission: https://steamcommunity.com/sharedfiles/filedetails/?id=270597861
  This supports distinguishing the in-game Silent Assassin rank from the
  separate all-zero challenge, but does not establish exact thresholds.
- CONFIRMED: copied C4-1, C4-2 and C4-3 main ZIPs to the existing local scene
  snapshot and verified copies against source SHA-256. C4-1 archive hash:
  `c7824aa0b3dce2b69ed3252efeb4bc978f7b05d59d8045a713c744cdaf333731`;
  C4-2 archive hash:
  `4678ae64d08e0df6ab59c9933e46b8a19218f7e406b868709dbef38b69064221`.
- CONFIRMED: the inflated C4-1 GMS contains Charlie Sidjan, a level-control
  script reference and `SidjanSurveilanceGuard.sdl` references. The four
  `rComputer` declarations found nearby belong to desk clerk records; their
  relationship to the surveillance monitor is not established. INFERRED: C4-1
  is Basement Killing, supported by the Charlie and surveillance guard names.
- CONFIRMED: inflated C4-2 GMS contains `WindowCrashAlarm.sdl` at 0x15c6d,
  `ComputerDestroyed` at 0x1724, and a level-control declaration containing
  `rWindowPos` and `rWindowSound` at 0x5959b. INFERRED: C4-2 is Graveyard
  Shift, supported by the level's system-administrator and skybridge references.
  Neither the identity of the required window nor its damage behavior is proven
  by these names; alarm effects and axe interactions remain OPEN.
- Prior hypothesis that melee damage alone would provide a suitable zero-stat
  route is incomplete: the owner reports melee strikes affect the desired
  result as well. Which visible counter or internal value changes, and whether
  the objects currently accept axe damage, remain OPEN. Do not equate zero
  shots with the complete challenge. The truck case requires tracing collision,
  death attribution and discovery alerts separately.

## Rating screen and reported patrol problem (follow-up)

- The user-provided screenshot shows `Shots Fired: 2`, zero for the other
  visible action counters and saves, and `Rating: Silent Assassin`. This is
  direct evidence of that displayed result, not of the mission, version, or
  how the shots were made. A guide's separate example of a perfect-run total
  with two shots is at https://steamcommunity.com/sharedfiles/filedetails/?id=270597861
  and must not be treated as proof of the screenshot's origin.
- The owner reports melee strikes can also affect the sought outcome, and
  describes attracting guard fire to destroy the object. This remains an
  unverified report for these builds; an exact changed field is unknown.
- CONFIRMED (static): for capture SHA-256
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`,
  REA `ev_9eae6431e0ad49b595c2501aac4154c5763345f567e231ead2453cd7f525944e`
  shows the function at VA 0x55bed0 uses values in two fields to index an 8 by
  8 table and resolve a ranking string. It also formats mission time. This is
  static decompilation, with unverified field identities and indirect calls;
  it does not establish a special all-zero rank or reveal melee accounting.
- The owner recalls an ordinary tunnel patrol crossing a truck route; the
  previously inspected C3-2a data contains both `PatrolGuard32.sdl` and
  `Van32a.sdl` references. Their actual coordinates and intersection are OPEN.
  A player guide independently reports a truck striking a guard and a later
  discovery: https://steamcommunity.com/sharedfiles/filedetails/?id=270597861
  This does not establish whether the kill counter, alert counter, or both
  increment in the user's build. The route/AI behavior, collision and body
  discovery should be observed as separate events.

## Conclusions and next steps

## Runtime run 1 — Basement Killing monitor (2026-11-16)

- Steam executable SHA-256 `f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136`,
  PID 24540; existing Enhanced release configuration has timer, resolution,
  menuscale, framelimit, hdfonts and settings enabled. Passive read-only
  `tools/watch_values.py`, log `samples/local/logs/exp006-statistics-watch.log`.
  The user performed the actions; no instrumentation was installed.
- Owner reports attempted axe strikes did not break the surveillance monitor,
  while a pistol shot did. The supplied completion screenshot shows Normal
  difficulty, one shot, one close encounter, zero alerts, kills and harms,
  zero saves and Silent Assassin. The screenshot does not establish what caused
  the close encounter, nor rule out other ways of damaging the monitor.
- CONFIRMED: after 199.157 s of the watch, sampled result globals for the
  pinned Steam build changed from zero to `field08=1 field0c=1 field10=0
  field14=0 field18=0 field1c=0 field20=0 field24=0 derived1=90 derived2=1`.
  The first two values match the two visible counters numerically, but their
  individual mapping is ambiguous because both are 1; derived field meaning is
  unverified. Polling does not show when the axe or shot happened. The log
  ended on process exit. No claim is made about a 1.01 build.
- Next: in Graveyard Shift, test the same axe action on the required window,
  note actual breakage, and capture the completion screen; use a fresh watcher.
  To determine statistics causality, compare controlled runs from an identical
  save with one changed event at a time, and capture the live producer rather
  than assuming these result-screen globals track events during play.

## Runtime run 2 — Graveyard Shift window (2026-11-16)

- Owner reports that the axe did not break the required window and that a
  pistol shot did. This is a controlled player observation of the attempted
  interaction, not a proof that every possible non-firearm method fails.
- Steam executable SHA-256 `f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136`,
  PID 11504, same read-only watcher in
  `samples/local/logs/exp006-window-watch-2.log`. The owner did not complete
  the level, so this run establishes neither final counters nor rating. The
  observed result-screen globals remained zero until process exit.

## Projectile activation in Basement Killing and The Graveyard Shift (2026-11-17)

- `CONFIRMED` The [All Zeroes listings](https://www.speedrun.com/hitman2/levels)
  include both missions, and [WerCZ's PC Graveyard Shift run](https://www.speedrun.com/hitman2/runs/z1pe0ogz)
  is verified as All Zeroes. The supplied [Basement Killing](https://www.youtube.com/watch?v=59KDIFuqxgs)
  and [Graveyard Shift](https://www.youtube.com/watch?v=OkC-OpX7K90) video pages
  were throttled by the web fetcher. Guard-fire sequence remains owner-reported
  in this investigation. The earlier owner test distinguishes Silent Assassin
  rank from zero visible action counters.
- `CONFIRMED` The two C4 scene snapshot archives match the SHA-256 values
  above. Using the inflated GMS and BUF, read the object-table offset at GMS
  +0, then the count and 8-byte entries at that offset. The low 24 bits of the
  first dword in each entry, multiplied by four, give the record offset.
  The first record dword indexes the object name in BUF. High-bit object refs
  resolve through the table after stripping the high bit and subtracting one.
- `CONFIRMED` C4-1 object 4365, `Screen_SurvCam_01`, at GMS `0x51DC0` in
  the `C4-1_SurveillanceSystem_01` group points to routine list `0x2740`
  and parameters `0x51D84`. The routine list points to
  `ZGEOM_ProjectileHit` at `0x272C` and `ZSTDOBJ_ShotActivate` at
  `0x1F30`. The parameter block references `LevelControl` (`0x80001120`),
  `Computer_Monitor_Shatter` (`0x800004F5`), and the screen itself
  (`0x8000110E`). This identifies the target screen rather than the desk
  clerk computers described earlier.
- `CONFIRMED` C4-2 objects 1191/1193/1194 (`ENABLED_Win_01/03/02`)
  use routine list `0x1DA4` with `ZSTDOBJ_ShotActivate`,
  `ZGEOM_ProjectileHit` and `ZGEOM_WindowNormal`. Parameter blocks
  `0x15C38`, `0x15D08`, `0x15D68` pair each pane with a matching
  `DISABLED_01/03/02` object group at the same serialized position.
  All three point to the same `WindowCrashAlarm.sdl` block at
  `0x15C68`. The role of individual script instructions and alarm accounting
  remains OPEN.
- `CONFIRMED` On Steam mapped capture SHA-256
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`,
  bounded LLVM disassembly at VA `0x4D59A0..0x4D5AC2` shows registration
  of `ProjectileHit`, an event-ID check in the corresponding handler, and
  dispatch to the configured receiver. `ZSTDOBJ_ShotActivate` separately
  registers `ProjectileHit` at `0x4B6050`, checks the ID at `0x4B6150`,
  and reads the scene impact record from the event payload at `0x4B6181`.
  The `CloseCombatAxe` dispatch at `0x51B412` calls `0x5198E0`,
  which constructs a separate combat action at `0x5263B0`.
  REA evidence:
  `ev_3f9916c80530ceab804bb9bb7253f159abce82d326613153edbdd958631bb5ec`,
  `ev_350e63adff4f831e13c2dc0410dec9d0fd0be9c54620fd1af390447f8b974939`
  and `ev_39ee5cc116ba5cfcfaa6a66d89a1a5e5388f20a9d6e25d6f7779d4d1195de721`.
- `INFERRED` The replayable break behavior is selected per object while
  the event handling is shared. Increasing the axe's damage does not
  connect the axe action to the projectile-driven behavior. Add a separate
  melee impact route scoped to the listed objects and reuse their break and
  mission effects. The prop routine needs the engine's collision record for
  the struck geometry.

### Experimental axe implementation

All addresses below are VAs in the Steam mapped capture with SHA-256
`f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`.
The redirect targets the on-disk Steam executable with SHA-256
`f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136`.

- `CONFIRMED` Bounded LLVM disassembly at `0x5260F5..0x52619B` shows two
  world-space sweep endpoints passed to the character collision query.
  The call at `0x526171` returns its boolean in AL. The callee at
  `0x526550..0x5267B0` has a 20-byte callee-pop ABI. The axe constructor at
  `0x5263B0` assigns vtable `0x657528`. The actor registration at
  `0x50ECE0..0x50ED07` assigns class `0x2003E8` to `0x6ADD40`.
  Each of the two main GMS tables contains exactly one entry of that class.
- `CONFIRMED` The projectile caller at `0x485856..0x485900` fills a trace
  with origin, displacement and a 1.0 limit. It calls the scene room's
  vtable at +0x2C4 using `(trace, 0, 2, -1, 1, 1)`. The ZROOM constructor
  at `0x44CA80` assigns vtable `0x643830`. This slot resolves to
  `0x4547A0`, which returns a boolean, pops 24 bytes and writes world
  contact at +0x28 and the geometry reference at +0x44 into the trace.
- `CONFIRMED` Rechecking `0x48503F..0x4850C7` shows the first message field
  comes from the trace pointer, and the next field is the projectile
  reference. The hit geometry's owner at +0x54 receives the message.
  The sender dispatch slot resolves to `0x43CBE0` and passes the payload
  directly to the receiver. The prop routines' readers at `0x4B6181` and
  `0x4D5A05` dereference the first field for geometry and contact.
  REA found the projectile callback entry through vtable slot `0x649638`
  (`ev_c3dc37604e7363cb23e5ade800c499e6bbf459f69cd8c74cfbe0dd3765559b86`).
- `CONFIRMED` Each target's pair of GMS record dword +0xC and object table
  second dword is unique in its mission archive. The monitor pair is
  `(0x6BB8FE, 0x1438)`. In pane order 01/02/03 the pairs are
  `(0x754D4, 0x392)`, `(0x7DDF4, 0x396)`, and `(0x7DCA4, 0x394)`.
  Reproduce by parsing the tables using the procedure above and counting all
  matching pairs. The loader at `0x41D128` passes the model id to
  `0x5B9690`, which stores it at base geometry +0x68. The loader writes the
  second value at +0x6C at `0x41D1CB`. See the Ghidra batch evidence
  `ev_659fcb990fb34d68ecf880032665e1ae02e7a6b2245a46798c07e7117e919e48`;
  the tool did not identify the axe update or endpoint helper as procedures,
  so those were checked with bounded LLVM disassembly.
- `INFERRED` The opt-in `axebreak` fix uses that native first-hit query on
  the axe sweep segment only when the original character sweep misses. It
  sends the impact message to the matching prop with the collision record and
  the actual melee actor. There is no projectile creation or counter write.
  The event runs at the original attack decision, before the game selects
  the miss animation. Contact uses the native sweep centerline and does not
  reproduce the full animated blade volume.
- `CONFIRMED` Win32 Release built successfully. All seven CTest tests passed.
  The Python suite ran 97 tests with one skip and no failures. The native test
  checks that the four intended targets are accepted and that mismatched
  scenes, models and tags are rejected.
- `OPEN` Verify axe reach and alignment, obstruction, break and progression,
  guard response and the final All Zeroes statistics in both missions.
  A log line naming a prop confirms dispatch only. The release defaults
  leave the patch off.

### Axe contact tolerance

- `CONFIRMED` Owner testing reported successful breakage in both missions,
  with a narrow angle and occasional need to step back. The local log
  recorded dispatches to the monitor and all three escape panes.
- `CONFIRMED` In the same Steam capture identified above, the axe endpoint
  helper at VA `0x526410` supplies a radius of 40. The caller at
  `0x526130..0x526150` uses actor vtable slot +0x88 to convert both points
  to world space. For ZHITMAN2 vtable `0x656B08`, the slot resolves to
  `0x43B880`, which forwards to the base-geometry transform.
- The updated option tries the original segment first, then up to six
  parallel segments offset horizontally within that radius. Near samples
  are shifted toward the actor, and side samples cover either side of the
  original path. Each segment takes its first world collision and the swing
  sends at most one impact. The log identifies the selected sample.
- `CONFIRMED` The owner reported that the wider sweep works for the windows,
  with the monitor still sensitive to angle. The log recorded all three
  panes and several monitor contacts on the wider-sweep build.
- The monitor now gets a fallback of five heights across three lateral lanes.
  The traces use the character's forward sweep depth and take only the first
  collision on each lane. This fallback runs only in Basement Killing.
- `OPEN` Owner confirmation of the monitor fallback and final All Zeroes stats.

### Retail 1.0.0.265 axe port

- `CONFIRMED` VAs below apply to executable SHA-256
  `0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560`.
  Masked matching against the Steam functions followed by bounded
  `llvm-objdump -D` identified the axe sweep call at `0x5244C1`, with the
  same aligned context `3f e8 da 03 00 00 84 c0` and callee ABI at
  `0x5248A0`. The axe constructor at `0x524700` installs vtable
  `0x6502A8`. Its endpoint function at `0x524760` has the same reach
  and radius. The original update converts the endpoints using actor
  vtable slot +0x88 at `0x52448A` and `0x52449A`.
- `CONFIRMED` The scene room vtable `0x63C5C8` has the matching collision
  function `0x4534C0` at +0x2C4. The projectile caller at `0x484348`
  uses the same trace layout and arguments. `0x4B4850` registers
  `ProjectileHit` at `0x6A3EA4`. The dispatch at `0x43B880` and
  receiver at `0x4B48E0` use the same calling convention and hit fields.
  The scene-name and geometry-reference helpers and ZHITMAN2 class are
  identified by their matching PE exports.
- `CONFIRMED` Retail C4-1 and C4-2 archive SHA-256 values are
  `723d84762642230107ce876648c8651ef50c31ec7440a6b3fc13541d10b57b45`
  and `eccb5a876eb9bbe7b12f23ce26c18949cf4fe1b2feb5b62b185d8c09ad82b43c`.
  Decode the two GMS tables as above. The monitor is now at index 4366.
  All four named targets retain their original model/tag pairs, each
  unique in its own archive. The retail loader at `0x41D8F8` and
  `0x41D99B` maps those fields to the same geometry offsets.
- `OPEN` Retail in-game axe contact, breaks, mission progression and stats.

## Static follow-up — vehicle and character events (2026-11-16)

After the owner declined the time-consuming Hidden Valley playthrough, the
pending passive watcher was stopped and analysis continued on the existing
Steam capture, SHA-256
`f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`.

- CONFIRMED (static): REA `ev_883b2fd3231b68f2190d86a45833d4ed2847d7511d33765e5c2445010e440e40`
  decompiles VA 0x594a70 as preparing a `VehicleHit` message, a vehicle object
  reference and other unidentified data, then performing an indirect call.
  At VA 0x594a93 raw bytes `c7 44 24 04 7c 45 69 00` load the string address.
  REA `ev_248c83c8857a0b4db4273c13d83d9da29b96468227762a7989c99b4c3832dc9c`
  shows VA 0x594e00 calls this function after two checks; their meanings and
  the receiver are OPEN. No link to a particular truck was demonstrated.
- CONFIRMED (static): in code identifying itself as
  `hitman2/characters/levelcontrol.cpp`, VA 0x50447f loads the
  `CharacterKilled` string (`68 60 b8 68 00`), and VA 0x504484 calls
  `RegisterZMsg`. Another bounded sequence at 0x5012b0 calls 0x505530 and
  dispatches `CharacterKilled`. REA
  `ev_937e1f67007759852bb8368107827a3309cafd8f9d6d06c6a93f53876e14cafa`
  shows 0x505530 increments different fields for argument 1 or 2, calls a
  helper with different constants and dispatches `ScriptCharacterKilled`.
  For example 0x50554e bytes `89 be 18 02 00 00` store one increment.
  Meaning of the categories, origin of the argument, whether these fields
  are the final player counters, and whether vehicle death reaches this path
  are OPEN. No player attribution or bug root cause is proven.
- Static procedure: `llvm-strings` on the capture to locate named message and
  source-path literals, direct-byte reference search, bounded `llvm-objdump`
  at 0x594a70, 0x594e00, 0x5012b0, 0x50446e and 0x505530, plus the REA
  records above. This identifies investigative entry points; runtime behavior
  and scene-specific patrol/vehicle path intersection remain unverified.

## Exploratory 3D mapping — C3-2a (2026-11-16)

- Input is the pinned C3-2a archive above. A read-only scene/PRM decoder
  `tools/hidden_valley_map.py` generates a local, offline interactive HTML at
  `samples/local/mission-investigation-20261116/hidden-valley-routes.html`.
  It reads 72 Arrow-named position records in table entries 3410..3624 and
  849 mesh vertices from the ten `TruckSpline_01`..`TruckSpline_10` records
  at entries 3922..3931. These are candidate patrol points and spline-named
  geometry, not proven trajectories. Their order, links and object sizes have
  not been reconstructed; the viewer intentionally shows points, not lines.
- Research sources: TheDodylectableX/io_scene_glacier commit
  `54f29b5d08efb3f432e1d53d19f42a79122f6244` provides PRM and GMS placement
  hypotheses; ReGlacier/HBM_GMSTool commit
  `c1519fa949f3f7136f57ca7cb02f4d748198ff50` uses a four-byte entity-table
  index. On this H2SA scene multiplying the index by four resolves 4646
  plausible named, positioned records versus 83 with multiplier 16; the
  scripts, names and positions corroborate but do not validate every record.
  HBM-specific format assumptions are not automatically applied to H2SA.
- OPEN: object transforms. Spline objects share position approximately
  (-48574, 0, 20668) and a matrix swapping the X and Z axes. Applying only
  translation creates visual proximity between spline vertices and patrol
  candidates; applying the stored matrix as a conventional row-major rotation
  places them differently. Neither interpretation is validated as the actual
  engine/world transform. The viewer exposes both rather than choosing a result.
- Under the translation-only hypothesis, the nearest sampled pair for spline
  `TruckSpline_10` is 276.7 game units apart in 3D (patrol table entry 3487
  around (-68876, -2349, 29734), spline mesh vertex around
  (-68925, -2213, 29969)); this is vertex-to-point distance, not distance
  between continuous paths or collision volumes. Without verified units,
  transforms, spline interpretation, actor paths, speeds and timing it does
  not demonstrate a crossing or collision. Other route points also lie near
  spline-named geometry under this hypothesis. No live observation was made.
- Reproduce with `python -I tools/hidden_valley_map.py
  samples/local/mission-investigation-20261116/C3-2a__MAIN.ZIP
  samples/local/mission-investigation-20261116/hidden-valley-routes.html`.
  The tool refuses other archive hashes. Browser rendering and transform toggle
  checked locally; the browser's missing favicon generated one harmless 404.
  Next identify the true transform and whether the geometry represents the
  actual vehicle centerline; then recover NPC waypoint links and account for
  extents and time before treating proximity as an intersection.

### Checkpoint references and proximity (follow-up)

- CONFIRMED (static scene bytes, same archive hash): the five `Van32a.sdl`
  records contain ten `rPath` references. Interpreting high-bit-set references
  as one-based scene-table indices resolves them in numerical order to
  `TruckSpline_01` through `TruckSpline_10` in all five records. Their
  `startPath` values are 8, 6, 4, 2, 0. This consistent resolution is strong
  evidence for the encoding, not proof of run-time order or timing.
- The four `CheckPointNinja.sdl` records have IDs 1, 0, 2, 3 and each has
  `examine`, `examine2` and `end` reference slots. Under the same one-based
  decode, the nonzero values resolve to the surrounding `Examine`,
  `Examine#0`, `Examine#1` or `End` objects. `m_Arbiter` resolves to an
  `Arbiter`. Some examination slots are zero. The actual script logic and
  semantics of the IDs remain OPEN.
- CAUTION: using the same decode, `Van32a`'s `rCheckPoint` resolves to a
  record named `Ground`, not the nearby object named `CheckPoint`. Do not
  treat every ref's semantic target as confirmed; investigate aliases,
  hierarchy, runtime interpretation or authoring before making changes.
- Nine checkpoint `Examine`/`End` positions and four checkpoint ninja positions
  are shown in the updated viewer. Under the translation-only hypothesis, all
  nine nearest sampled vertices belong to `TruckSpline_03`. One `Examine#0`
  (table entry 3528) is 82.8 units away in the horizontal XZ plane from its
  nearest sampled vertex and 154.1 units below it; 3D distance 174.9 units.
  This is a candidate overlap zone, not proof that vehicle and guard collision
  bodies intersect. No assumptions about scale, collision mesh, time or
  transform are made. Matrix interpretation gives a different layout.
- CONFIRMED (static executable, Steam capture SHA-256
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`):
  the vehicle code registers `VehicleTempStop` and `VehicleContinue` at
  0x593d89 and 0x593db3. Manually bounded handler at 0x594160 compares
  incoming message IDs with those fields; 0x5941e9 (`e8 92 11 00 00`) calls
  0x595380 in the stop branch, and 0x594240 clears a field in the continue
  branch. Ghidra did not create a function at 0x594160; this is bounded
  disassembly, not a verified full function boundary.
- REA `ev_149cc68b38b547517c64e01983acab975e6458241af5fe3e35529d567005cea3`
  shows 0x595380 compares absolute values of three supplied coordinates to
  three object fields. The vehicle-hit path 0x594e00 also calls it before
  sending a hit event (see `ev_248c83c8857a0b4db4273c13d83d9da29b96468227762a7989c99b4c3832dc9c`).
  INFERRED: this is a box-like spatial test. The coordinate frame, tested
  object, boundaries, events, and its role in an actual jam remain OPEN.
  Helpers in the two stop/continue branches appear in REA
  `ev_04ea0960402cde250cc8fac600f716458dcbd2efb16d1810b66676eb008241c5`
  and `ev_fefd683bc47b310ca6d3075f9b14e63bf90c501f88e6ff1d65d152d95c7cdaa5`;
  behavior names have not been independently established.
- CONFIRMED (static, same capture): the stop branch reads a float from the
  message payload at +8 into vehicle field +0x1c0. The bytes at 0x6397d4
  (`00 00 80 bf`) encode -1.0, and the x87 comparison at 0x594207..0x594212
  selects a special case for that value: 0x594214 writes `0x7e967699`, about
  1e38f, to the field. In the update block 0x5945b0..0x5945f6, positive
  field values have a float subtracted from them; on reaching the negative
  branch the field is set to zero and the same helper as `VehicleContinue`
  is called. The continue-message branch explicitly clears that field and
  calls the same helper. INFERRED: -1 requests an effectively indefinite
  stop until an explicit continue, as ordinary small decrements cannot change
  a float around 1e38. The source of the decrement is not independently named.
  This is not itself proof of a bug: whether this mission sends -1 and whether
  a continue message is lost or blocked remain OPEN. Reproduce by bounded
  `llvm-objdump` on 0x5941e2..0x594254 and 0x5945b0..0x594606; no code was
  patched and no run-time stop event was captured.

Both reports warrant separate investigations. OPEN: reproduce each behavior,
identify exact version and mission assets, establish whether the displayed
statistics and rating both change, and trace the responsible event and state.
For Hayamoto, compare the same kill and guard-kill cases across verified 1.01
and 1.02 installs, including their mission assets; differences cannot be assumed
to reside in the executable. For Hidden Valley, record the truck event, body
discovery, counters and rating separately, then compare with a player-caused
death and an independently discovered body. Check frame rate as a controlled
variable rather than assuming causation.

OPEN: Whether either fix is feasible without adverse effects or content changes.
The two owner-performed object tests are described above; no patch or game-file
modification was performed as part of those tests.
