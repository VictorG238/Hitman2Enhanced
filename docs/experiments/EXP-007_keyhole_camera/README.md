# EXP-007 - Keyhole camera steering

## Goal

Determine why the PC version's door-keyhole view is reported to face only one
direction, whether the shipped PC engine already contains a steerable camera
path, and what the narrowest safe fix would be.

## Status

Static root-cause candidate confirmed on 2026-11-17; an off-by-default `keyhole`
prototype is now implemented. Original game files have not been changed.
The reported PC behavior and the HD Trilogy comparison have not been
independently captured at runtime.

## Inputs

| File | SHA-256 | Role |
|---|---|---|
| `samples/local/dumps/steam-001/main.analysis.img` | `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a` | Read-only mapped Steam process capture; all VAs below apply only to this image |
| Installed `Hitman2.ini` (read only, 2026-11-17) | `907eb217c725a7e3c8f83c8b2839aa15d6d01c8ed6948c95ba2256067ba44054` | Current local input configuration; contains `UseDirectInputMouse` and no `UseGameController` |
| Installed `keyboard.cfg` (read only, 2026-11-17) | `4eecb5f866f1f58078f434709e0bef8a3506c796d1a473333881e70c3115914f` | Current local overrides; contains only Map bindings, no scope/keyhole bindings |

The installed configuration hashes describe this machine at the observation
time; they are not game-version identifiers.

## Tools and procedure

1. Search the mapped image for `keyhole`, `PeekHole`, `Scope*`, door messages,
   and their source-path strings.
2. Use REA/Ghidra 12.1.4 to trace exact string references and decompile the
   smallest containing functions.
3. Follow the `ZHM2ScopeCamera` mode dispatcher into the mode-3 update path.
4. Resolve the input virtual call through the `ZSysInputWintel` and
   `WintelGameController` vtables.
5. Corroborate Ghidra output with bounded `llvm-objdump` listings and exact byte
   reads from the captured image.

Example reproduction:

```powershell
llvm-objdump -d --no-show-raw-insn --start-address=0x417260 --stop-address=0x417360 `
  samples/local/dumps/steam-001/main.analysis.img
llvm-objdump -d --no-show-raw-insn --start-address=0x4edc00 --stop-address=0x4f1c00 `
  samples/local/dumps/steam-001/main.analysis.img
llvm-objdump -d --no-show-raw-insn --start-address=0x411c70 --stop-address=0x411d50 `
  samples/local/dumps/steam-001/main.analysis.img
```

## Results

### Separate keyhole camera path

- `CONFIRMED` The image contains `ZHM2ScopeCamera`, `PeekKeyHole`,
  `MSG_PEEKHOLEREQUEST`, `MSG_REQUESTKEYHOLEPOS`, and the original source paths
  `camera\\hm2scopecamera.cpp` and `background\\doors.cpp`.
- `CONFIRMED` Door dispatch `FUN_004ce630` routes `MSG_PEEKHOLEREQUEST` to
  `FUN_004ce210`, which builds the keyhole request data, and routes
  `MSG_REQUESTKEYHOLEPOS` to `FUN_004ce3f0`.
- `CONFIRMED` The scope-camera message handler at the manually delimited block
  around VA `0x4EFBDB` selects keyhole mode by writing `3` to `[this+0x15A]`.
  The per-frame dispatcher `FUN_004eeca0` calls `FUN_004eee80` only for mode 3.

REA evidence: `ev_268a462e810e8a83de373cc94e02231335a54fd25d1f805591781d9e7c69cacd`,
`ev_c8e162242966715cfffd9c77a7f5c23d83601aee40f81091bd9e01ef320ef419`,
`ev_1c15395ab50bb3226dbe060dcdb77cdf2476c1310895ad4d5cff70a21e50138e`,
`ev_c606429b8269096e6174719fd9a6b2df0aa4a393095fa1229f9cb6ce277d16f1`,
and `ev_d291cb2b25e8b7180d5d426a9ec6b6b2813a4b53bc0f976bf756862c6d00b527`.

### Steering is gated by controller mode

- `CONFIRMED` The config parser block at VA `0x4172D5` queries
  `UseGameController` and, only when present, writes `1` to `[this+0x60]`.
  Exact bytes at `0x4172D5`:
  `8b0d5c6c6a008b118d44241050686c226800ff525484c07404c6466001`.
  REA: `ev_654ebb96f97f31c53e8873bd5ea16b5f551eac4a6c4c9789177f8ec8e82d6177`.
- `CONFIRMED` Keyhole update `FUN_004eee80` reads
  `[g_pSysInterface+0x60]`. If it is zero, control jumps directly to the
  close/request handling at VA `0x4EEF84`; no camera rotation is calculated.
  Exact prefix at `0x4EEE80`:
  `a15c6c6a0083ec44568bf18a486084c90f`.
  REA: `ev_539374b64f1d6ccc976a640742a9d616ae94be352f2e635f1cce8e08f7e02842`.
- `CONFIRMED` When controller mode is enabled, the same function calls
  `g_pSysInput->vtbl[0x2C]` using device index `[g_pSysInput+0x20]`, reads two
  axes from the returned state at `+0x04` and `+0x0C`, scales them by the float
  at VA `0x6527BC` (`-0.2243995`), constructs an absolute deflection relative to
  the stored door basis (correction: initially described as incremental), and
  submits the new camera transform through the camera object's vtable slot
  `+0x80`. REA: `ev_3cff344a3de8cc0145c8245f71efa66e26795765ac8795605b8e7e72a766ecfd`,
  `ev_f9f9bb5ea74e1bbae3027256d16a2540bd80a239aae1c497bf448239954fa015`,
  `ev_3300f5f1eb16da473015cb2748e6d0f125afc7e121f613b3b64a2cbbe0bc2f88`,
  and `ev_010157461f25f59f4a4df065edae5168bae14423aaffa499fa73b45f741929a0`.
- `CONFIRMED` `ZSysInputWintel` vtable `0x639C10` resolves slot `+0x2C` to
  `FUN_0040fa00`, which returns a selected input device's state pointer.
- `CONFIRMED` `WintelGameController` vtable `0x639CD8` resolves its state getter
  to VA `0x411EB0`, exactly `lea eax,[ecx+0x42]; ret`. Its update beginning at
  VA `0x411C20` fills four normalized axis floats starting at `+0x42`; the
  keyhole reads the second and fourth floats. This is compatible with one
  two-axis controller stick, but the physical-stick identity remains
  `INFERRED`. REA: `ev_9467c2e16e1c6e62f6801f5062e50ee3c3efa9e528923d5d33a4aa73893c4cb3`
  and `ev_1642d5cdd28702c96fe157f1f2aeb84fb7a06f97516eb824ffa8a36ee53aecc1`.

### Mouse path and action names

- `CONFIRMED` The mapped image contains `ScopeLeft`, `ScopeRight`, `ScopeUp`,
  and `ScopeDown`. Ghidra found no direct code references to `ScopeLeft` or
  `ScopeRight`. `ScopeUp`/`ScopeDown` are read by `FUN_004f0b60`, a shared
  scope-aim input function, not by the mode-3 keyhole update.
- `CONFIRMED` The keyhole function does not fall back to `WintelMouse` deltas
  when `UseGameController` is absent. It skips the steering block entirely.
- `INFERRED` This controller-only gate explains the reported fixed PC keyhole
  while also explaining how a console/controller build could expose steering
  without replacing the entire keyhole camera system.

REA evidence: `ev_a4186399eb757d7ae1edcef27214c2583ff9db8de8a765f00045d4902c94c6a8`,
`ev_bbf07422dfcaf2018143fa2a9ab9ebd6ce9429162557fc84a5c382694076897e`,
`ev_887967cc36ad1f5ee6d9b9e33c35afc592fb48e2d38a5f88d10b1c88f9a238ff`,
`ev_a7d8d36982b6b5191e248e7fd06ab236cbf9cdf3b178978089c207b6d2fa1528`,
and `ev_8c5eb05a169345cfd1dd93393b1eea7d51aa48c66dfd79a20359ed5ab338b9bd`.

## Conclusions

- `CONFIRMED` This is engine/input behavior, not a per-level door or geometry
  defect.
- `INFERRED` A PC mouse-steering fix is technically feasible because the
  shipped keyhole path already owns a two-axis rotation implementation.
- `CONFIRMED` Adding `UseGameController` to `Hitman2.ini` is not an acceptable
  fix: it enables controller mode globally, while the desired behavior is
  limited to the keyhole camera.
- `HYPOTHESIS` The narrow fix should preserve `FUN_004eee80`'s existing camera
  math and feed it normalized mouse `dx/dy` only while scope mode is 3 and
  controller mode is off. The signs, sensitivity, angular limits and door-side
  behavior require a short runtime prototype before implementation can be
  considered correct.

## Open questions and next proof

1. `OPEN` Whether the HD Trilogy build uses this exact controller branch or a
   later camera implementation; no console binary or capture was analyzed.
2. `OPEN` Whether the existing rotation is clamped elsewhere and whether it
   permits viewing through the door mesh at extreme angles.
3. `OPEN` The correct mouse sensitivity and inversion relative to normal camera
   settings.
4. The implemented prototype remains excluded from release defaults until a
   door test validates both sides of a door, exit handling, and controller behavior.

## Prototype (2026-11-17)

- `CONFIRMED` Rechecked the capture SHA-256 above with Python hashlib and the
  bounded update/dispatcher disassembly with llvm-objdump. The dispatcher E9 at
  VA `0x4EED49` targets `0x4EEE80`; aligned context at `0x4EED48` is
  `5ee93201000083f8`. The complete update `[0x4EEE80,0x4EEFB3)` has SHA-256
  `8b6d171ef716653b2304f7eb3c71fece63d43a35ce954edd23de75dcf0b5bd81`.
- `CONFIRMED` The bridge reproduces `sub esp,44h; push esi`, sets ESI=self,
  EAX=axes and stack vector `{0,0,1}`, then resumes at `0x4EEEBD`. The original
  suffix restores ESI, adds `44h` and returns at `0x4EEFB2`. It transforms the
  direction through the stored basis at self+`0xF2`, not the previous deflection.
- `CONFIRMED` WintelMouse update `0x411810` copies its relative float deltas
  into sys+`0x7F0` and sys+`0x7F4` at `0x4118BC`/`0x4118CA`. Prototype uses
  those values, accumulates at 0.01 stick units/count and clamps to [-1,1].
  It skips the entry frame and resets on exit, door change or a gap in updates.
- `CONFIRMED` Installation remains untouched: the developer installer reports
  an existing unmanaged `d3d8.dll`, so its refusal to overwrite is preserved.
- `OPEN` Runtime signs, clipping, sensitivity/inversion preferences and controller
  fallback. Automated bridge tests do not validate these gameplay properties.

Reproduce build/checks: `cmake --build build --config Release`,
`ctest --test-dir build -C Release --output-on-failure`,
`python -m unittest discover -s tests`.
For a game test, enable `[keyhole] Enabled=1` with this build; enter a door peek,
move horizontally/vertically, stop moving, exit/reenter and repeat on the other
side. Startup application and entry/exit messages appear in `h2e.log` at Level 1.

### Verification outcome

- `CONFIRMED` Win32 Release build succeeded. All six native tests passed across
  the initial run and the corrected `keyhole_test` rerun. Python: 95 tests,
  one optional snapshot test skipped, no failures.
- The first ABI harness tried to reserve the game's fixed VA, which was already
  occupied in the test process. Corrected the harness to copy the compiled bridge
  and relocate only its continuation operand, following the timer harness pattern.
  It now passes without depending on process address-space layout.
- `OPEN` No game launch, gameplay capture or deployment was performed. The built
  artifact is `build/Release/d3d8.dll`; the installed unmanaged proxy remains intact.

### Owner-authorized Steam installation (2026-11-17)

- `CONFIRMED` Subsequently installed at the owner's explicit request with the
  installer's new `upgrade` action. Previous proxy/config/fonts/settings/log backed
  up under `samples/local/install-backups/pre-keyhole-20261117` before replacement.
  Upgrade rollback and registry tests: 22 passed.
- `CONFIRMED` Installed DLL SHA-256:
  `c8f6e80970844cc5b6cc72d814148fee7e771624d7ae9afb012cbf8f2c2df4a2`.
  Compared installed bytes to the build and verified config/font hashes against
  the installation marker. Original Steam executable hash unchanged.
- Enabled: resolution, timer, menuscale, framelimit, hdfonts, settings, keyhole.
  Horplus remains off because of its documented HUD defect. Logging Level=1;
  existing user settings preserved. No game launch; runtime validation still OPEN.

### Owner video comparison (2026-11-17)

- `CONFIRMED` Inspected the owner's Xbox 360 reference video (11.6 s, 1920x1080,
  60 fps) using ffprobe and ffmpeg frame samples. The view sweeps across the side
  walls, ceiling and floor and repeatedly returns to the same frontal door view.
  The recording does not show controller input, so release timing is owner-reported.
  Derived contact sheets are local-only under `samples/local/keyhole-video/`.
- `CONFIRMED` The installed PC log records the keyhole redirect applied and
  repeated view entries/exits. The owner reports that the PC sweep is too small.
  This establishes application of the hook, not console behavior parity.
- `CONFIRMED` The prototype imposes [-1,1] itself and accumulates mouse deltas
  without recentering on idle. These are prototype choices, not evidence for the
  Xbox 360 limits. Its original-controller-range claim applies only to the
  analyzed PC path. Matching the video requires revisiting range and input mapping.
- `OPEN` Exact Xbox angular range and return response cannot be obtained from
  these frame samples alone; no Xbox binary or simultaneous input capture analyzed.

### Expanded mouse range (2026-11-17)

- `CONFIRMED` At the owner's request, mouse deflection now clamps to +/-3.5
  engine units instead of +/-1, corresponding to approximately +/-45 degrees
  with the existing pi/14 scale. Sensitivity, idle retention and controller path
  are unchanged. This is a chosen PC range, not a measured Xbox range.
- `CONFIRMED` Release build and all six native tests passed. Installed through
  `upgrade`, with previous files backed up under
  `samples/local/install-backups/pre-keyhole-45deg-20261117`.
  Installed DLL verified against build, SHA-256:
  `b5ecedb4b6f7e611b258ef0ec17f651ff9ef295563224cf9be5820a484326076`.
- `OPEN` Owner gameplay check of the enlarged sweep and door geometry clipping.

### Reduced range after owner test (2026-11-17)

- Owner confirmed geometry intersection at some 45-degree positions and requested
  +/-35 degrees. Mouse clamp changed to +/-49/18 engine units, keeping sensitivity.
- `CONFIRMED` Release build and all six native tests passed. Installed after the
  owner closed the game, using `upgrade`; backup:
  `samples/local/install-backups/pre-keyhole-35deg-20261117`. Installed DLL matches
  build, SHA-256 `c46634365e69c58aaf9ec51321b0e77921410d26efa5ee28a21b961066211731`.
- `CONFIRMED` Owner screenshots also show an upper strip with a straight horizontal
  boundary, containing scene/door imagery. Owner reports it becomes black after
  opening and closing the menu. `HYPOTHESIS`: stale pixels outside the active scene
  viewport, rather than solely the camera intersecting door geometry. Existing
  viewport hook does not clear the render target; menuscale changes viewports only
  while its menu detector is active. This inspection does not establish the writer
  or prove the origin is in the base game versus the proxy.
- `OPEN` Upper-strip cause/fix and in-game validation of the 35-degree limit.
  This deployment changes only the angular range; it does not fix the strip.

### Stale top-strip correction (2026-11-17)

- Binary: Steam mapped capture SHA-256
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`.
  `CONFIRMED` Bounded llvm-objdump of VA `0x4EF520` shows the setup passing floats
  `{.35, 0, .30, .85}` to camera vtable slot `+0x220` at `0x4EF56C`.
  Immediate stores at `0x4EF549/551/559/561` encode
  `3eb33333/00000000/3e99999a/3f59999a`, respectively.
- `INFERRED` These specify the keyhole rectangle in bottom-origin coordinates;
  D3D viewport is x=.35W, y=.15H, width=.30W, height=.85H. At 1080p the top
  boundary is y=162, matching both supplied screenshots. Pixels above that
  boundary persisting until a menu redraw strongly suggest missing color clearing.
  The game viewport and causal correction have not yet been captured live.
- Implementation: on the known scene projection callback, after a current-frame
  mode-3 update, clear only x=[viewport.X, X+Width), y=[0, viewport.Y) to black.
  Require matching rectangle (2-pixel rounding tolerance), scene projection and
  back-buffer target. Temporarily use a full viewport for Clear, restore it, and
  do not touch projection, depth/stencil, camera range or scene pixels.
  This avoids painting over the final mask or menu at Present.
- `CONFIRMED` Win32 Release build and seven native tests pass. New real-device
  `keyhole_surface_test` fills the back buffer with a sentinel color, invokes the
  production helper, reads all pixels and checks that only the top strip changed.
  It also checks viewport restoration and rejection of fullscreen, shadow-sized
  and matching-size offscreen targets. No game launch involved.
- `OPEN` Owner validation: enter the same keyhole before opening a menu; top
  strip should be black immediately and remain so after closing the menu.
  Log success: `keyhole: cleared stale top strip (scene viewport ...)`.
- `CONFIRMED` Installed through `upgrade` and compared installed DLL to build:
  SHA-256 `c7ea23f7d11a753621833e6bd5bd8a79450ac844ddaf4e3563f539250fc1775c`.
  Prior build/config backed up under
  `samples/local/install-backups/pre-keyhole-topstrip-20261117`.

### Nearby geometry limit (2026-11-17)

- `CONFIRMED` The Steam capture (SHA-256
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`)
  contains a collision query used by the normal camera. Bounded LLVM disassembly
  at VA `0x4C6BB6..0x4C6C2B` shows its world pointer loaded from
  `[[g_pSysInterface+0x98]+0xAD8]`, the query storage at caller stack
  `+0x154`, and calls to constructor `0x5CA4B0` (ret 16), gather
  `0x5CA570` (ret 16), and segment test `0x5CA7F0` (ret 12).
  The normal camera releases the query with `0x5CA560` at `0x4C77E5`.
- `CONFIRMED` Gather copies the origin from its first argument and the basis
  from its second, with extents third and force-update fourth. The segment
  helper passes the origin and unnormalized displacement to `0x5CA2C0` and
  returns true for the first intersecting triangle in the segment. See the
  `0x5CA755..0x5CA7E4` and `0x5CA821..0x5CA874` listings. REA cross-reference
  from `mouseconcam.cpp` to the camera code:
  `ev_4e3ac3c68f668fcbc7466ac0c818b28099f3e3b08eedd45f70aaca5808566a43`.
- Implementation uses the same camera basis helpers as the keyhole update
  and the actual keyhole projection from the prior frame. On a nonzero turn
  it gathers local triangles once and tests five short segments per sampled
  pose. Sweeps advance by about four degrees and refine the first obstruction.
  The query and transform bodies are hash checked. Camera position is fixed.
- `OPEN` In-game obstacle coverage and frame cost. This is sampled collision
  detection and cannot detect geometry missing from the engine collision mesh.
  Log markers: `keyhole: geometry limit enabled` and
  `keyhole: nearby geometry limited camera movement`.
- `CONFIRMED` Win32 Release and 7 native checks passed. Python suite ran 97
  tests with one optional skip using a workspace-local TEMP directory. The
  sweep test covers a blocked interval with a clear endpoint, free motion,
  and retreat from overlap. The native game query itself was not run in these tests.
- `CONFIRMED` Installed with the developer installer and verified the DLL
  matches build SHA-256
  `1cd8bc5b580afd81f5c807818cfff89c17baf1548020bfcdb03a48ce980435ef`.
  Backup: `samples/local/install-backups/pre-keyhole-collision-20261117`.

### Full-height keyhole scene (2026-11-17)

- `CONFIRMED` Owner reports the collision limit works. The Steam log records
  collision blocks and the previous strip clear with viewport `672,162 576x918`
  at 1920x1080. The owner reports that the rectangular edge remains visible.
  The shared screenshots show the mask opening beginning around y=102, above
  the first scene row y=162. Blackening those uncovered rows still leaves a seam.
- `CONFIRMED` In the Steam mapped capture SHA-256
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`,
  the mode dispatch at `0x4EFFB5` routes mode 3 to `0x4F000A`.
  Its setup call at `0x4F0047` targets `0x4EF520` (REA xrefs
  `ev_54c2a75e5c1052cf32227f6ad4a12d16c6dbae2ce1734f852b4806373af64531`).
  The fourth viewport argument at `0x4EF565` is `.85f` (`9a99593f`).
  Change it to `1.0f`
  (`0000803f`) in the running process. The startup patch verifies setup
  bytes and exchanges the aligned word at `0x4EF564` with other threads
  suspended. No additional clears are needed during rendering.
- `CONFIRMED` 7 native and 31 targeted Python tests passed. The D3D8 test
  confirms pixels above the old boundary are now inside the scene viewport,
  with the adjacent area excluded. It confirms the collision viewport
  classifier accepts the new rectangle and rejects offscreen targets.
- `CONFIRMED` Installed DLL matches Release build SHA-256
  `4588be5321c4cd4143ef1d9a1ce872f23df2ce8b0f4f2815425539659ef524a2`.
  Backup: `samples/local/install-backups/pre-keyhole-full-height-20261117`.
  `OPEN` Gameplay appearance and collision coverage with the new vertical framing.
  The expected Steam log rectangle at 1080p is `672,0 576x1080`.

### Movement along collision edges

- `CONFIRMED` The prior limiter applied the first blocked fraction to both
  mouse axes. The owner reported that the view could stick at a side edge
  until moved sideways, even when trying to look up or down.
- The updated limiter sweeps the requested turn first, then tests the
  remaining pitch and yaw independently from the last reachable pose. If a
  shallow overlap blocks a single-axis turn, it tries up to one degree
  toward center on the other axis. It accepts that correction only if it
  clears geometry and permits progress on the requested axis.
- `OPEN` Owner verification at the doors that previously stuck.

### Retail 1.0.0.265 keyhole port

- `CONFIRMED` The following VAs apply to retail executable SHA-256
  `0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560`.
  Masked matching and bounded `llvm-objdump -D` identify the mode-3 tail
  at `0x4ED409` (`e9 32 01 00 00`) and update at `0x4ED540`.
  The shared x86 bridge resumes at `0x4ED57D`, with the same stack
  and register inputs as Steam. Input at `0x40FA4B..0x40FA73` writes the
  same mouse fields at system +0x7F0/+0x7F4.
- `CONFIRMED` Retail viewport setup at `0x4EDBE0` has the same first
  0x52 bytes and viewport height word at +0x44. The retail normal camera
  caller at `0x4C5456..0x4C54CC` confirms the scene world layout and
  calls the collision family based at `0x5C67C0`. Matched rotation and
  basis helpers are at `0x5BE870` and `0x5BF250`. The port validates
  the retail body hashes before enabling these paths.
- `CONFIRMED` Win32 Release builds. All seven native tests passed,
  including the bridge with a build-selected continuation.
- `CONFIRMED` The retail installation reports all eight requested fixes
  enabled. The deployed proxy matches the Release build SHA-256
  `b860b01e148ddbdbd211d6a6b27828848c8982f5d77ea73dcfc05ec5877444cd`.
  The installer verified a backup in
  `samples/local/install-backups/retail-1.0-keyhole-axebreak-20261117-183701`.
- `OPEN` Retail in-game mouse input, full-height viewport and collisions.
