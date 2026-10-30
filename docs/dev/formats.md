# Killzone HD (NPUA80856) file formats

All multi-byte values are big-endian. Samples come from the decrypted
`data_ps3.psarc` (docs/GETTING-FILES.md); extract with
`python3 tools/psarc_ls.py <psarc> <outdir> <substring>...`.

This note comes from an earlier stage of the project, a hand-written viewer that read the data directly. The
dumpers it names (`core_dump.py`, `tex_dump.py`, `mesh_dump.py`, `skeleton.py`, `level_dump.py`, `polysoup.py`) are
not published; the layouts are kept as documentation of the formats. The port itself reads none of this: the game's own
code does.

## .snz: raw PCM audio (5300 files)

| Offset | Type | Field |
|---|---|---|
| 0x00 | u32 | channels (1 or 2) |
| 0x04 | u32 | tracks (1-4; >1 only in `interleaved.snz` music files) |
| 0x08 | u32 | sample rate (8000-48000, mostly 14000 for dialogue) |
| 0x0C | u32 | frames |
| 0x10 | s16[] | PCM, big-endian |

`16 + frames * 2 * channels * tracks == file size` holds for all 5300 files.
Track interleaving order is not yet confirmed. (The converter is not part of this repository.)
The EBOOT builds `.snz` names in `0x85878` (sound resource constructor).

## .core: serialized engine objects (424 files)

Lumps under `LocalCache.PS3/Lumps/`. KZ1 HD engine format, not Decima.
`tools/core_dump.py` parses the whole container; it reads all 424 files
exactly to EOF (139,266 objects, 165 classes).

The reader works on a stream with "align to 4" before every u32; strings and
blocks are read whole and then walked with the same rule.

### Header and string table (EBOOT `0xba2bc`, `0xbc77c`)

| Offset | Type | Field |
|---|---|---|
| 0x00 | char[16] | magic `Core<1.79><PSX3>` |
| 0x10 | u8 | flags: 0x4 = companion `<name>.CoreStream` exists (268 files have 5, 156 have 1) |
| 0x14 | u32 | string block size (block may end with a few zero bytes of slack) |
| 0x18 | u32 | string count |
| 0x1C | | strings: u32 length + bytes, each length 4-aligned |

Strings hold class names, property names and string values; everything else
refers to them by index.

### Groups (EBOOT `0xbbca0`), repeated until a u32 `0xFFFFFFFF`

One group per class. `u32 size` then a header block of that size:

| Field | Notes |
|---|---|
| u32 class name index | e.g. `Texture`, `SoundResource`, `BlendShapeAnimator` |
| u32 object count | |
| u32 schema version | per-class |
| u32 property count | then 16 bytes per property: `u32 name index, char[4] type tag, u32 info, u32 offset` |
| u32 has GUID, u32 has stream | |
| per object | `u32 data size, u32 unknown`, then 16-byte GUID if has GUID, then `u32, u32` stream location if has stream, then an extra `u32 start, u32 end` pair for classes the engine flags with 0x8000000 (seen only on `WeaponPickupResource` and `WeaponModelSetResource`) |
| zero padding | so object data starts 16-aligned |

Then each object's data: 16-aligned, `data size` bytes. The engine creates
the class by name (`0xa744c`, RTTI factory) and calls its virtual
`Deserialize` (vtable slot 0x18) on the bytes, so payload layout is per class.

Property type tags: `BOOL FLOA CORE STR INT UINT PATH RGBA MPRJ FVC2 FVC3 BBOX
UNT1 UNT2 FRNG EAMT EWPT KZEF`. On `CORE` (object reference) tags the high bit
of each letter forms a 4-bit flag (seen: 2, 4, 9), most likely the reference
kind. `info` looks like `kind << 28 | size` (`0x10000001` for BOOL,
`0x40000004` for 4-byte values, `0xFFFFFFFF` for strings). `offset` is the
field offset inside the in-memory object (`0xFFFFFFFF` = not stored); it is
not an offset into the file data.

Most common classes: BlendShapeAnimator 85k, SoundResource 9.2k,
SkeletonAnimationResource 7k, AITalkStateImmediate 6.4k,
BlendShapeAnimationResource 5.3k, Material 2.6k, AnimationState 2.4k,
Texture 2k, StaticMeshInstance 1.7k.

Open points: the per-object `unknown` u32, the meaning of the stream pair
(offset/size into `.CoreStream`?), and each class's payload layout.

### Texture payload (7973 unique textures, `tools/tex_dump.py`)

Object data = 18 big-endian u32 (72 bytes), pixel data (all mips, largest
first), then an 8-byte trailer.

| Word | Field |
|---|---|
| 3 | name string index (usually just `Texture`) |
| 4 | `CoreTextFilename` string index (`source:<path>.CoreText`, used for output names) |
| 5 | `PS2Mip1Distance` (float) |
| 6 | `PS2MipMapL` |
| 8, 9 | width, height |
| 10 | format: 21 DXT1, 23 DXT5, 7 A8R8G8B8, 5 32-bit (one 8x8 texture), 27 placeholder (dims `0xFFFFFFFF`, no pixels) |
| 12 | mip count |
| 13-16 | repeat of width, height, format, mips |

DXT blocks are stored as on PC (little-endian colors). 32-bit textures are
ARGB with RSX swizzle (Morton order). Verified visually on HUD, weapon,
character and level textures. 7959 textures decoded
(382 MB as PNG, kept outside any repository); 13 are placeholders. Format 22 (DXT3) is mapped in the tool but not seen.

### RegularStaticMeshResource payload (9787 meshes, `tools/mesh_dump.py`)

Deserializer: `RegularStaticMeshResourcePS3` vtable `0x555718`, slot 6 ->
`0xd475c` (calls the base resource reader `0xb9c18` first). Vertex arrays are
read by `0xf6158`, index arrays by `0xf63a0`.

| Offset | Field |
|---|---|
| 0x00-0x3F | base resource fields (sort order, lightmap density, max visible distance, bounding box...) |
| 0x40 | u32 primitive count |

Per primitive (4-aligned):

| Field | Notes |
|---|---|
| u32 material | object id of a `Material` (same ids as the per-object `unknown` u32 in the group header, so that field is the object id) |
| u32 vertex count, u32 vertex format, u32 (always 1) | then `count * stride` bytes |
| u32 index count, u32 index format (1 = u16) | then u16 indices, triangle list |
| 3 x u32, 2 x u8 | unknown, then align 4 |

Vertex format bits, attributes in this order (all big-endian):

| Bit | Size | Attribute |
|---|---|---|
| 0x80 | 12 | float3 position (always set) |
| 0x40 | 12 | float3 normal |
| 0x20 | 4 | RGBA8 color |
| 0x2000 / 0x4000 / 0x6000 | 4 | packed attribute, meaning unknown |
| low 3 bits | 8 each | number of float2 UV sets |

All 9787 static meshes parse with in-range indices (4.8M triangles). The
`.obj` export keeps the first UV set and groups faces by material id.
Meshes are in local space; placement comes from `StaticMeshInstance` (not
decoded yet).

### RegularSkinnedMeshResource payload (1451 meshes, 1.14M triangles)

Deserializer: `0x1a28c4`. Same base header, then:

| Offset | Field |
|---|---|
| 0x2C | Skeleton id |
| 0x30 | 4 x u32, unknown |
| 0x40 | u32 bone-group count (always 0 in KZ1 HD; the reader supports nested groups, `0x1a1de4`) |
| 0x44 | primitives, same layout as static meshes |

The vertex format of skinned primitives has no position bit; the first 12
bytes of each vertex are a runtime slot (garbage in the file) filled by the
skinning code. After the primitives comes a tail:

    u32 primitive count
    per primitive:
        u32 vertex count, then per vertex 4 bone index bytes + 4 weight bytes (weights sum to 255)
        u32 vertex count, then per vertex float3 position + float3 normal (bind pose, model space, Z up)

`mesh_dump.primitives(b, skinned=True)` patches the bind-pose positions into
the vertex slot, so skinned meshes export like static ones. All 1451 parse
with in-range indices.

### Skeleton (`tools/skeleton.py`, deserializer `0x193788`; 337/337 parse)

After a variable-size header (hull reference list) comes an unaligned big-endian
byte stream: `4 x u32, 6 x u8, i32 attachment count, i32 bone count`, then

- attachments (`bullet_eject`, `muzzle_flash`, `flag`...): `16 f32` 4x4 matrix, string, `i32` bone index
- bones: `12 f32` inverse bind (3x3 rows + translation, row vectors), `6 f32` hull box, `2 x i32`,
  `4 x u8` (byte 2 = parent index, 0xFF for the root), `3 f32` local offset, three strings
  (name, parent name, hull name; strings are `u32 length + bytes`), `2 x i32`.

Helper bones (`Root_bone`, belts, bags, `gun_bone`...) have a 0.05 scale in
their inverse bind; the parser divides it out, which keeps the joint position.
Vertex bone indices are skeleton bone indices.

### SkeletonAnimationResource (`skeleton.parse_animation`; 183/183 of `Entity.Player.3rd.Templar`)

Word 5 = `CoreTextFilename` (e.g. `.../rifle/animations/stand_step_back.CoreText`),
word 11 = duration (f32), word 16 = `SkeletonAnimationBoneMap`. Stream at 0x50: `u32 track count` (= bone count), then per track
`u32 flags, u32 key count`, keys of `f32 time, 4 f32 quaternion
(x, y, z, w), [3 f32 translation], [3 f32 scale]` (flags 0 = rotation only,
1 = + translation, 2 = + translation + scale; 2 appears in the emplaced gun
animations, 161 of 1444 in `AllWeapons.Default`). Values are local (parent-relative) bone
transforms; constant tracks keep a single key.

Track order comes from the `SkeletonAnimationBoneMap`: its word 4 is a string
index, and that string lists the bone names one per line, in track order.
It is close to a breadth-first walk of the skeleton but not equal (e.g.
`gun_bone` comes before `Neck_Bone`), so the names must be used.

Entity export (`level_dump.py --entity`) writes the skeleton as a glTF skin
and every matching animation as a glTF animation (`viewer.html?anim=<name>&at=<s>`).
Verified visually: tactical stance and crouch-aim poses hold the rifle
correctly.

`HumanoidActorResource` ties a character together: word 6 = BaseModel
(`MultiSkinnedMeshResource`), word 13 = Head Normal (`LodSkinnedMeshResource`,
bound to a second skeleton with the same bone names but slightly different
inverse binds, exported as its own glTF skin on the shared joints). Weapons
are static meshes in `AllWeapons.Default.core` (e.g.
`weapons/isa/m_82_rifle/third_person/model.CoreText`) whose model space is
the `gun_bone` space; `--entity <out> <Entity.core> <AllWeapons.core>
<path part>` attaches one. Helghast animations are not in the Helghast
entity lumps (likely in `AllWeapons.Default.core`, 1444 animations).

Containers: `LodSkinnedMeshResource` / `MultiSkinnedMeshResource` carry a
Skeleton id at word 12 and the mesh ids after it (word 0 = count). Entity
lumps (`Entity.*.core`) export with `level_dump.py --entity`, which takes
the first `MultiSkinnedMeshResource`; the first multi of
`Entity.Player.3rd.Templar` has no head part.

### Placement and materials (`tools/level_dump.py`)

Object ids (the per-object u32 in group headers) are unique inside a file;
shared objects are duplicated across lumps (38k of 139k ids repeat), so
resolve references within the same `.core`.

Two classes place render meshes, both with a row-major 4x4 float matrix used
with row vectors (`p' = p * M`, translation in the last row):

| Class | Size | Matrix bytes | Mesh resource word | Notes |
|---|---|---|---|---|
| `StaticMeshInstance` | 96 | 12-75 | 19 | props; word 1 = name string index |
| `CollisionMeshInstance` | 112 | 20-83 | 21 | most of the level's world geometry (render mesh + `CollisionResource` at word 3), usually identity matrix |

- `LodStaticMeshResource`: word 0 = LOD count, words 12.. = mesh ids
  (first = most detailed), followed by LOD switch distances (floats).
- `MultiStaticMeshResource`: word 0 = part count, words 12.. = mesh ids.
- `Zone` lists `StaticMeshes` per zone (visibility); not needed for export.
- `Material`: layers of 16 words starting at word 8:

  | Layer word | Field |
  |---|---|
  | 0 | Texture id |
  | 3 | flags; top byte = blend mode |
  | 4 | low byte = UV set (only set for lightmaps; prefer word 12) |
  | 6 | bits 16-23 = blend source: 0 = layer alpha, 0x04-0x06 = vertex color channel (terrain splats; channel order is a guess) |
  | 12 | high half = UV set |
  | 1 | flag 0x20000000: 6 more words follow the 16 (a 2x3 UV transform, floats `a b c d e f`: u' = a u + b v + e, v' = c u + d v + f; tiling camo x8, rotated detail maps). All-zero ones mean none |

  Blend modes, inferred from the textures they carry: `0x00`/`0x0b` base;
  `0x05` blended layer (base when first); `0x04`/`0x0c`/`0x0e` decal by
  alpha; `0x06`/`0x07`/`0x12` lightmap (multiply; `0x06` carries the bright
  "4bit" occlusion maps of weapons and vehicles, mean ~0.5-0.8, multiplied 1x,
  the others are dark baked lightmaps scaled 2x); `0x0d` environment map
  (`metal_gloss`, `fisheye`); `0x03`/`0x08`/`0x13` additive or particle
  effects (glow, fire, smoke). Common stacks: base + lightmap (463),
  terrain base + 3 splats + lightmap (72).
  Lightmaps (`lm-*_lum`) are dark DXT5 (mean 20-45/255) and must be read
  as linear values scaled up (~3x by eye) before multiplying.

Export: one glTF per level (all lumps merged, world space, Y-up, one
primitive per material layer stack). Raw UV sets 0-3 go to `TEXCOORD_0-3`,
vertex color to `COLOR_0`, the layer stack to material `extras.layers`
(`tex`, `mode`, `uv`, `src`); `baseColorTexture` is also set for other
tools. `tools/viewer.html` blends the layers in a shader
(three.js; `.claude/launch.json` entry `kz-levels` serves
`work/kz1hd/npua/levels/`). Meshes larger than 300 units are flagged
`extras.backdrop` (sky shells) and hidden by default.

Level 1 = lumps `Level.1.Trenches`, `LightIndustry`, `Industries`,
`HQGrounds`: 735k triangles. Lightmap scale (`lmi`, default 3) and the
splat channel order are guesses until compared with RPCS3 captures.

### Ladder

112 bytes: words 3-18 = placement matrix (translation = foot of the ladder),
word 19 = `LadderResource` (may be 0), word 20 = height in meters (u32).
Level 1 has two (LightIndustry 8 m, HQGrounds 4 m).

### Sky bands and fog shells

Meshes over 300 units are sky geometry: horizon silhouettes (`horizon_1_1`,
black alpha masks), fog shells (`fog_1_anticlip`, `fog_1_3`) and far smoke
billboards. Placed in the level they hang mid-air; the game draws them in its
sky scene. `plain_white` shells with vertex color ~0.25 hugging the ground are
fog volumes for the game's depth fog. The runtime skips those, fogs the bands
out completely and applies a sepia distance haze (color and density matched by
eye to the reference videos).

### SpawnPoint

112 bytes: word 3 = name string (`Spawn_Player`, `Spawn_HGH1a`, `Spawn_ISA2Ch`...,
also `AIMarker_*`), bytes 16-79 = placement matrix (row vectors), then
`AI.Radius`, multiplayer team flags and `Spawnable.Actors` (a string such as
`"HGHBaseRifle"`). Level 1 has 244 spawn points. The runtime maps name
prefixes to entity lumps (`Spawn_Player` -> `Entity.Player.3rd.Templar`,
`Spawn_HGH` -> `Entity.Helghast.3rd.Assault`, `Spawn_ISA` ->
`Entity.ISA.3rd.Soldier`); the real actor table is not decoded yet.
Soldier lumps carry no animations; human skeletons share the 94 bone names,
so the runtime remaps the Templar animations onto them by name.

### AI navigation (`runtime/src/nav.rs`)

`AITerrainManager` points at the per-lump navigation data:

- `AIWaypointGrid`: 4 words, u32 count, then per waypoint `3 f32` position
  (about 0.45 m above the ground) + `2 u32` (flags; 0, 2, 66, 64, 195 seen).
- `AIWaypointNeighbors`: 4 words, u32 count, then per waypoint `u16 count +
  u16 indices`, padded to 4 bytes. Links are **one-way** (in level 1 the
  Helghast side reaches the player's trench but not the reverse); median link
  2 m, max ~6 m. Bytes after the lists are not decoded.
- `AIWaypointVisibility` (per-waypoint visibility bits), `AIAreaGraph` /
  `AIArea` (areas listing waypoint ranges, `Containment.Waypoints`) and
  `AINearestWaypoint` are not used yet.

Level 1: 1315 + 1427 + 1365 + 2078 = 6185 waypoints. The runtime merges the
lumps' graphs, and alerted Helghast re-plan every 2 s towards the waypoint
reachable from them that is closest to the player (A*), jog along it, and stop
to shoot within 20 m with line of sight. In a 20 s simulation a Helghast went
around from 27 m to 18.8 m and started firing.

### Mission scripts (`KillZoneScript`, `runtime/src/lua4.rs`, `runtime/src/mission.rs`)

Each sublevel has `Mission.<level>.<sublevel>.MissionScript.core` holding one `KillZoneScript`
object: a compiled **Lua 4.0** chunk (source name `levels/level_1_the_trench/sublevel_trenches/mission_script.lua`).
The chunk follows lua-4.0 `lundump.c` with these differences:

- the `"Lua"` signature is dropped: header `1b 40 01 04 04 04 20 06 09 04`, then the test number
  314159265 as a big-endian f32 (numbers are f32, everything big-endian);
- one extra opcode, `STMT`, sits at index 13 (between `GETGLOBAL` and `GETTABLE`): no operands,
  no stack effect, emitted at statement starts. All later opcodes shift by one (`CLOSURE` = 49).
- no debug info (local names are stripped).

`tools/lua4dis.py <core> [--calls|--src]` disassembles, lists the engine API used, or prints a
rough decompile. Level 1 / Trenches: 391 functions. The chunk's top level runs ten closures that
define the shared library (`CreateSquadFromArray`, `AddHGHBaseRifle`, `AddPlayer`, actor settings
tables such as `gHGHBaseSoldierSettings` with `mModelBaseName = 'Helghast.3rd.Soldier.Variant'`)
and the level's entry points:

- `OnLoad`: `gGameInterface:LoadEntity('Helghast.3rd.Soldier')` etc. (lump `Entity.<name>.core`),
  speech, sounds;
- `OnInitialize`: player (`AddPlayer('Spawn_Player')`), globals;
- `OnUpdate`: called every tick; a `Timer` global counts ticks and drives the sequence
  (Timer 180: tank and first tutorial; HGH1 dead: next wave...). It also drains `gMsgQueue`
  messages (`GetID` / `GetSenderName`): `Trigger_Up`/`Trigger_Down` from named area triggers and
  delayed messages (`AddDelayedMessage(seconds, id, ...)`);
- `OnArea_*`: called by `ScriptCallFunction` objects in the level (word 6 = function name, an
  `AreaTrigger` object id among words 2..4);
- `IsFinished` / `OnFinish`, `OnCheckPoint` / `OnCheckPointRestore`.

Lua 4.0 has no booleans: the scripts define `TRUE = 1`, `FALSE = nil`.
Actors are created with `gGameInterface:CreateActor(name, type, model, aiResource, faction,
controller, spawnPoint)`; humanoids use type `Humanoid` and model `<mModelBaseName><n>`, the
`HumanoidActorResource` whose name (word 2) matches. Orders (`OrderAdvance(marker)`,
`OrderMoveTo`, `AlertFor('combat')`...) name `AIMarker` objects.

Level objects the scripts use:

| Class | Layout |
|---|---|
| `AIMarker` | word 0 = name, words 4..19 = matrix (position at words 16..18) |
| `AreaTrigger` | word 2 = name (`AreaTrigger_TankShoot1`, or just `AreaTrigger`), word 5 = allowed activation count (0 = unlimited), word 14 = trigger area object id, words 15..18 = triggered by neutral/friendly/enemy/player |
| `BoxArea` / `SphereArea` | word 1 = name, words 3..18 = matrix; the box is the unit cube [-1, 1] under it (`General.Scale2X`), the sphere's radius is the row 0 length |
| `ScriptCallFunction` | words 2..4 include the input trigger id, word 5 = script name (`MissionScript`), word 6 = function |

Script behaviours the runtime reproduces (from reading the scripts; engine side inferred):

- AI orders are queued: `Start(Prioritized)CommandSequence` clears the queue, orders until
  `EndCommandSequence` run one after another, and `OrderReportToScript(id)` posts message `id` only
  when the orders before it are done (e.g. Sanchez opens the barbed wire with
  `OrderUseOrderedObject('PlayAnimArea_BarbedWire')`, which makes the area post `PlayAnimArea_Start`).
  Outside a sequence an order replaces the queue. Tank `OrderAttackMarker` fires at the marker.
- Area triggers: only the mission sublevel's own triggers count (every sublevel lump has
  `ScriptCallFunction`s calling same-named `OnArea_*` functions). Words 15..18 (triggered by
  neutral/friendly/enemy/player): 1 = yes; 256 (only on `OnArea_ISASave`) is taken as "ISA soldiers
  only" (a buddy reaching cover advances the script). The player's feet are tested.
- `ToggleObject` (word 2 name, words 3..18 matrix, word 19 skinned mesh, words 20/22 default and
  toggle-up animations): barbed wire gates and doors; `Toggle()` plays the opening once.
- `HumanoidActorResource`: the head is the first `LodSkinnedMeshResource` after the body (word 13
  on player characters, word 8 on Sanchez and the named extras; soldiers' heads are in the body).

Level 1 plays its four sublevels in a row: each mission's `OnFinish` calls
`gGamePlayInfo:SetLevelFile('1.LightIndustry')` when `GetGameStatus()` is won; HQGrounds names
`2.Offices` (level 2). More engine behaviour the later sublevels need:

- Trigger messages carry who entered: `GetObjectName()` is the player's name (`Templar`) or the
  buddy's; Industries and HQGrounds dispatch every `AreaTrigger_X` this way (`OnEnterTrigger`)
  instead of `ScriptCallFunction` objects.
- A talk state that finishes posts its own name as a message (`IsDialogueFinished(dialogue, msg)`
  compares it with the dialogue's `EndStates`); `General.Next States` chain the lines of a dialogue.
- `ceCreateObject('CountdownTimer')`: `SetAttribute('Duration', s)`, `Start`, `HasElapsed`,
  `SecondsRemaining`. Spline movers also post `AddEventTag(param, message)` messages.
- Squads: dead members leave (`GetSize` counts the living; HQGrounds sends APC2 and the tank when
  the earlier squads are down). Vehicles carry soldiers: `LineUpSoldier(soldier, slot)` puts them
  inside, `DeploySoldiers()` lets them out.
- Player spawn: `AddPlayer(spawn)` passes the sublevel's own name (`playerspawn`,
  `spawn_playerstart`, `SpawnPoint_Player`).
- `WeaponPickupInstance` (words 3..18 matrix, word 19 `WeaponPickupResource`: word 3 weapon name,
  word 5 mesh, word 10 rounds); `AddWeaponPickup(weapon)` makes soldiers drop theirs. First-person
  weapon models are the Multis `source:weapons/<name>/first_person.CoreText` in AllWeapons.
- When `IsFinished` is true but the script set no game status, the runtime counts the level won if
  every objective it was given is done (LightIndustry ends this way).

The runtime runs the scripts in its own Lua 4.0 VM at 50 updates per second: the scripts count
their own timers down by 0.02 per `OnUpdate` (e.g. artillery `delay - 0.02`). Calls it doesn't model yet are accepted and logged once.

### Collision (`tools/polysoup.py`, `data::level_collision`)

- `CollisionResourcePolySoup`: word 5 = collision group, word 6 = CoreText, words 7..12 =
  local bounds (twice), then a bounding-volume tree (not decoded), then `u32` polygon block size,
  polygons `u8 material, u8 n, u8 flags, n x u32 BE index`, `u32` vertex count, `3 x f32` vertices,
  padding. The block is located by its size prefix and the vertex count after it (100/100 soups
  of Trenches, 73/73 of Industries parse; 85,836 triangles in Trenches).
- `CollisionResourceBox`: word 5 = group, words 7..12 = local bounds (min, max).
- `CollisionMultiResource`: word 5 = group, words 7..12 = bounds, resources from word 13.
- `CollisionInstance`: word 0 = group, word 5 = resource, matrix from byte 24.
  `CollisionMeshInstance`: word 1 = group, word 3 = resource, matrix from byte 20.
- Groups (inferred from what they contain): `0x04000000` player (invisible walls around the
  playable area are 0x04 only), `0x08000000` AI, `0x10000000` shots and sight; terrain is
  `0x1c000000`, barbed wire `0x0c000000` (bullets pass). A resource's own group wins over the
  instance's (the terrain multi is 0x1c but holds the 0x04 walls). The low bits (0x2, 0x6) are unknown.

The runtime moves the player against the 0x04 triangles and casts shots and lines of sight
against the 0x10 ones (`--render-collision` uses the render meshes instead, the old behaviour).

### Runtime soldier AI (not from the game's data yet)

Helghast that know where the player is pick cover among the AI waypoints: a spot 8..30 m from
the player where a crouching soldier (eye 1.0 m) is hidden and a standing one (1.65 m) can see,
close to them and to their ordered marker, not taken by a squad mate. In cover they hide ~1.5 s
and pop up to fire ~2 s. ISA buddies shoot the nearest Helghast they see. The game's own cover
data (`AIWaypointVisibility`, waypoint flags, `AIArea`) is not decoded yet.
Third-person animations used: `crouch_combat_zero_heading`, `stand_fire_zero`,
`stand_jog_combat_zero`, `stand_idle_scan`, `stand_fall_*`.

### Paths, movers and artillery

- `PathInstance`: word 1 = name (`PathInstance_FlyBy1`), words 3..18 = matrix (identity in level 1),
  word 19 = `PathResource`. `PathResource`: 7 header words (word 5 = CoreText), then samples of
  8 floats: position, rotation quaternion (xyzw), step (0.04). Level 1's tank path has 281 samples
  over 172 m.
- Scripts drive vehicles with `gGameSystem:CreateSplineMover(path)`, `AttachTo(actor)`,
  `AddDestinationTag(param, name, ?)`, `SetMaxSpeed`, `GoToDestinationTag(name)`. The runtime takes
  the parameter as sample index * 0.04 and the speed in parameter units per second (fly-bys: tag 10
  at speed 0.4 = 25 s for 456 m).
- Vehicles: `Entity.Helghast.3rd.HeavyHoverTank` has one `RegularSkinnedMeshResource` on the lump's
  `Skeleton`; `DropShipLowDetail` a `MultiSkinnedMeshResource`. The script creates them with
  `CreateActor(name, 'Vehicle' | 'DropShip', 'Helghast.3rd.HeavyHoverTank', ...)`.
- Artillery: `gGameInterface:FireProjectile('Projectile.Mortar', nil, marker, 80, 60, 6, 15)` from
  `OnUpdateArtilleryStrike`. The runtime reads the 6th argument as scatter and the 7th as damage
  radius (a guess). Impact textures: `effects/mortar_impact/textures/{exploglow, explosion_cloud2,
  dirt, smoke7}`; the game's particle systems (`ParticleSystemResource`) are not decoded.

### Localized text, dialogue and fonts

- `Localized/<Language>/<Table>/SimpleText.dat` (psarc root): repeated little-endian
  `u32 id, u32 key length, key, u32 text length, text`. Scripts refer to `$<Table>.<Key>`
  (`$Level_01_1.Obj1` = "Defend the foxhole"). `ShowTutorial(n, ...)` indexes the `Tutorial`
  table in file order. Texts embed `<buttonimage function=reload>` tags for controller buttons.
- Dialogue: `LoadSpeech('$Language$.Story.Level_1_1')` loads lump `Custom.English.Story.Level_1_1`.
  Its `AITalkStateImmediate` / `AITalkStateWaitForLine` objects hold, after the
  `CoreTextFilename` word: priority, lifetime, **subtitle** (string), show-subtitles flag, **sound
  name** (`SENTENCE2313`); the name (`11_Sanchez_1`) sits in the words before. The matching
  `SoundResource` (name `SENTENCE2313`) has `Sound.Filename` =
  `localized/english/level_01_1/sanchez_4593` = `LocalCache.PS3/<that>.snz`. The scripts'
  `dialogueStruct` maps a dialogue id (`01_01_San_1`) to speaker + talk state;
  `StartDialogue` finds the speaker (`gAIManager:FindIndividual`) and calls `AddTalkState`.
- `Font` objects (`interstate12`...) hold glyph metrics only: 96 records of
  `u32 0, u16 x, u16 y, u8 width, u8 height, u16 0` into a 256-wide atlas (rows of 28 px), then
  `u32 count` kerning pairs `u16 left, u16 right, i16 adjust`. No glyph image was found in them
  or elsewhere (image fonts such as `imagemapsmall` embed a DXT5 texture header instead), so the
  runtime draws HUD text with a system TrueType font.

### Native runtime (`runtime/`, Rust + wgpu)

`cargo run --release` in `runtime/` opens level 1 straight from the decrypted
`data_ps3.psarc` (default path in `src/main.rs`, `--psarc` to change). Level
geometry, characters and weapons share one pipeline: static vertices use bone
0 of an identity palette, characters get a per-actor palette posed on the CPU
each frame. `--shot out.png --cam x,y,z,yaw,pitch [--time s]` renders one
frame offscreen; `KZ_DEBUG_VIEW=base|layers` swaps the fragment output for
debugging. Material stacks that start with a blend layer (`0x05`) are prelit:
texture x vertex color (bytes B, G, R, A) x 2.

Player: starts at `Spawn_Player` (matrix row 1 = facing, the models' +Y
forward) in walk mode: sphere-vs-triangle collision against every placed
level mesh except the >300-unit sky bands (grid of 4 m cells), gravity, 0.45 m
step-up, jump. `F` toggles free fly. `--walk <s>` simulates walking forward
before a `--shot`. The sky bands (`fog_1_anticlip`, `horizon_1_1`, mode 0x05
with alpha) are drawn alpha-blended last over a hazy clear color; the real
`SkyScene` is not decoded.

First-person view model: in `AllWeapons.Default.core`, the M82 model is the
`MultiSkinnedMeshResource` textured from `first_person/weapons/rifle/` (57
bones incl. `Weapon_Bone`, `Muzzle_Bone`, `camera_loc` and the arm bones);
the arms come from `Entity.Player.1st.Templar.core` (48-bone skeleton whose
bone names are a subset; joints are remapped by name). Its 29 animations are
the SkeletonAnimationResources under `first_person/weapons/rifle/`
(`pose_stand`, `stand_fire_primary_single`, `stand_reload_primary_v1`...).
`camera_loc` axes are X right, Y up, Z backward: the model is placed with
`inverse(camera_loc) * [right, up, -forward, eye]`. Third-person soldiers use
the 175 animations under `third_person/rifle/` from the same lump (idles,
hits, falls). Firing: ray from the eye against the collision grid and two
spheres per actor; a hit plays `stand_fall_back` once. `--fire <s>` fires at a
given time before a `--shot`.

### SoundResource (audio)

The audio is inline MP3: after the header (`Sound.Filename` string index,
volume, min/max distance...), a block `u32 byte count, u32 1, u32 1, u32
sample rate, u32 sample count` and a `0xFFFFFFFF` marker, MPEG-2 Layer III
frames run to the end of the object (the EBOOT loads `libmp3dec`). Decoding
`m82_fire(0)` gives exactly the 15967 samples at 16 kHz the header announces.
`AllWeapons.Default.core` has 118 SoundResources (81 distinct names; `name(n)`
/ `name_rnd(n)` are random variants). The runtime plays them with rodio:
M82 fire and reload, Helghast `lar_base_fire_rnd`, `Die_fallback_rnd`. The
`.snz` PCM files are the dialogue/ambience side.

Runtime AI (placeholder, not the game's): Helghast with line of sight within
60 m turn to the player, play `stand_fire_zero` and shoot every ~1-2 s with a
distance-based hit chance (5 damage); health shows in the title bar and the
player respawns at 0. `--ai <s>` simulates it before a `--shot`. Out of
range (> 20 m) they jog in (`stand_jog_combat_zero`), snapped to the ground
and stopped by walls; there is no pathfinding, so they get stuck on terrain.

Weapon/HUD (runtime only): 32-round magazine, automatic fire every 0.11 s while
the button is held, reload with R or right click (`stand_reload_primary_v1`,
blocks firing for its duration), `m82_emptyclick` when empty. The muzzle flare
is the static model `weapons/player/m_82_modular_system/effects/flare` attached
to `Muzzle_Bone` (its material is an effect layer) and drawn additively for
50 ms per shot. The HUD is placeholder rectangles (crosshair, health bar,
magazine ticks, reserve bar); the game's own HUD assets are not decoded.

### Other EBOOT entry points

- `0xa75e8`: opens a `.core`, checks the header, opens `.CoreStream` when flag 0x4 is set, calls the parser.
- `0xba1ac` / `0xba220`: reader constructor / destructor (object size 0x48).
- `0xba384`: reads a u32 string index and resolves it.

`.CoreStream`: starts with `<CoreStreamData>`; holds streamed bulk data for its `.core`.

## Static analysis

The addresses in this note come from a static analysis of the decrypted EBOOT in Ghidra (language
`PowerPC:BE:64:64-32addr`, import stubs named from their NIDs). The helper scripts used are not published. Note:
Ghidra marked `0xa7e78` no-return, which cut the `.core` loader short; clearing that flag regrows its callers.

## Animation graphs

First-person arms (weapons, ladder, emplaced guns) and characters are driven by state machines stored in
the `.core` lumps. `runtime/src/animgraph.rs` reads and runs them. Word offsets are 32-bit words of the
object data.

**AnimationStateManager**
- w2: number of states.
- w5: name. w6: diagram path (`source:models/.../animationdiagram.CoreText`). w7: default state.
- w8 onward: the states. After them come the action names and the variable names, each `\n`-separated.

**AnimationState**
- w0: number of switches. w3: name. w8: resource node. w9: AnimationStateEvents.
- Switches start at w12, 16 words each. Offsets within a switch:
  - +3: target state.
  - +4: action (empty for none).
  - +5: time in the state before it may switch (s).
  - +7: 0, or 1-3 on fire / reload / grab switches. Meaning unknown; these are ordinary switches.
  - +9: flag bytes A B C. A: the action must be held (1) or released (0). B: wait for the end of the animation (each wrap for a looping one).
  - +10: condition.
  - +13: AnimationStateEvents played while in the source state (timed sounds, `FOV 90`...).
  - +14: crossfade time (s).
- The first switch that passes is taken.
- Difference states ("... Delta") and Partial states play over the state they came from. Each base state has its own copy of them that switches back to it.

**Conditions**
- Keywords: `AFTER(n)`, `BEFORE(n)`, `AT(n)`, `BETWEEN(a,b)`, `EACH(n)`, `EACHOFS(n,o)`, `FINISHED`, `DURATION`.
- Operators: `! && || == != < > + -`.
- Quoted names are actions (1 while active) or variables.
- Time is counted in frames at 24 per second.

**AnimationStateEvents**
- w0: number of text events. w1: number of sounds.
- Entries start at w6, 4 words each: [name, or a RandomSoundResource whose w17 is the sound name; time (s); condition].
- The engine registers the text event names at 0x1c7b94. `FOV 90` and `FOV 60` set and clear a flag on the first-person model (0x1cc904).

**Resource nodes**
- Composite: w0 count, w7 loop. Items of 6 words from w10: [node, condition, weight]. Difference items are added on top.
- Directional: w0 count, w5 loop. Items of 4 words from w8: [node, _, value]. The variable name follows the items.
- TimeMapped: w7 child, w8 min, w10 max, w14 variable. The variable picks the time in the child.
- Difference: w6 a, w8 b. Result = a - b.
- Partial: w8 child.
- Sequential: w0 count, items of 4 words from w8.
- Simple: w5 events, w6 child.
- Loop modes: Off, On, Hold, PingPong (0-3).

**Ladder**
- Its clips play root-relative: Root_Bone is the player.
- When the state changes, the model is re-anchored so the root stays put.
- Climbing has no root motion. The player rises as far as the gripping hand moves down the body.
- `Ladder Top Switch`: 1 selects the turn-round top mount (`mount_from_top_reversed`) and the frontal top dismount. Set it from the Ladder's ReversedTopMount.

## First-person materials and view

Character and weapon material stacks, as the runtime draws them:
- **`lm_` maps with an all-black color:** some characters' and weapons' `lm_` maps hold their picture only in the alpha channel (the M82's `lm_templar_rifle`).
  - On a blend layer (mode 0x05) such a map is shading: white is lit, black is in shadow, so the color is multiplied by the alpha.
  - Its atlas lies on UV set 2. The layer's own UV byte says 0 on most of the M82, where it lays the map like the tiling base texture, which shows as camouflage stripes.
- **Mode 0x0d:** a reflection map on UV set 3, added at the base texture's alpha (the "with_ref_mask" textures) times 0.3. The 0.3 was set by eye.
- **Glass:** a stack that is only a reflection map (mode 0x03 or 0x08) is added over what is behind it and tinted by the vertex color. Examples are the M82 sight's lens, which comes out green as in the video, and the glass on the grenade shell.

**View:** the vertical field of view is 46.8°. That is the game's "FOV 60" text event read as horizontal on a 4:3 screen, and it puts the first-person M82 where the reference video has it (chapter 6, start). The "FOV 90" event, which the melee strikes send, is 73.7° the same way, and only the first-person model uses it.
