# Controller support in the frontend menu

The Controls tab uses the new frontend throughout. The original menu remains in
source for comparison; no second controls window is required.

## Restored behavior

| Original feature | Frontend behavior |
| --- | --- |
| Populate bindings for connected pads | New device profiles receive BAR defaults automatically, before input profiles load. SDL normalizes supported Xbox, PlayStation and other controllers. |
| Original gamepad layout | A/B use the south/east face buttons, L/R use the shoulders, Z uses the left trigger, the left stick steers and the right stick supplies C directions. Saved custom mappings are preserved. |
| Explicit Auto-configure | Controls > Auto-configure assigns connected pads in SDL order, up to four, then the keyboard to the next free port. Existing mappings are retained. |
| Play without configuring first | Fresh configurations merge keyboard input into player one; connecting a pad supplies its own initialized profile. |
| Reusable named profiles | Edit a player, select a binding template, then Apply template. Save new template snapshots current mappings; leaving the name blank generates one. |
| Template maintenance | Manage selected template exposes explicit replacement, rename and confirmed deletion. Names are unique per device type, ignoring ASCII case. |
| Custom map follows the physical controller | Selecting a profile on a player card copies gameplay bindings to the pad's independent saved working map. Reconnecting or reloading retains that association. Editing/deleting the source template does not change an applied copy. |
| Keyboard and controller editing | The mapping editor's keyboard/controller toggle also exposes a player's merged keyboard fallback in multiplayer. |
| Distinct identical pads | Existing serial/ordinal identities remain separate. Imported GUID-only assignments can reconnect even when a controller supplies a real serial. |
| Saved controller ports | Imported input.json assignments and deliberately empty ports are retained. A completed manual assignment supersedes that layout for the current session and survives hotplug. |
| Controller Pak / Rumble Pak | Existing per-player Accessories settings and modeled rumble remain in use. |

Applying a gameplay template preserves Accept, Back, Toggle Menu and other menu
bindings. Reset to defaults retains its existing whole-profile reset behavior.
Missing configuration fields receive defaults; explicitly cleared bindings stay
cleared. The original input.json is retained during migration to controls.json.

## Assignment details

Ordinary hotplug discovery keeps surviving controllers in their current ports.
An imported or manually assigned port remains reserved while its pad is unplugged.
The Assign players modal takes precedence while open. Auto-configure is an explicit
request to rebuild the session's player order, without replacing custom bindings.

New manual/Auto-configure player ordering is session-local, as in the frontend's
existing assignment model. Controller templates and device mappings persist in
controls.json; imported legacy port ordering remains the restart baseline.

## Validation

Run from the repository root on a configured Windows frontend checkout:

```powershell
powershell -File scripts/test-runtime-input.ps1
powershell -File scripts/test-frontend-controllers.ps1
```

The first harness exercises production profile/persistence code with isolated JSON
fixtures and virtual SDL controllers, including template copy/delete/overwrite,
menu-binding preservation, partial configurations and device association reloads.
It does not load real saves. The second checks the production defaults registrar
and the same pure assignment policy used by the event loop: automatic discovery,
manual gaps, imported reconnects, identical-device reservations and the dedicated
keyboard seat. Production menu/frontend compilation is checked separately.

Physical controller interaction and visual menu layout still need a bounded
hands-on check. Automated fixtures do not claim hardware gameplay qualification.

Validation at this checkpoint: both regression scripts pass, and the changed
production Controls page and frontend translation units compile. A complete
application rebuild/link was not performed: the local build folder lacks the
complete archive/executable closure and its build plan contains 965 steps. No
claim is made that an existing packaged executable contains these changes.
