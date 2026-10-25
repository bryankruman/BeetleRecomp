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


## raphnet dual N64 adapter (ADAP-2XN64-USB)

The frontend recognizes the manufacturer defaults for dual N64 firmware 3.3–3.6
(USB vendor `289b`, product `0030`, `0036`, `003c`, or `0064`). Each player interface
becomes its own SDL controller and receives an independent profile. All four C
buttons stay independent, including simultaneous opposing presses. The physical
A/B, Z, L/R, Start, D-pad and main stick map to their N64 equivalents.

A accepts menu selections, B goes back, and L/R switch tabs. During gameplay,
press L+R+Start together to open settings; release before using the chord again.
Normal Start remains the game's pause button. Binding capture and unfocused
windows suppress this shortcut. Existing SDL mappings and saved custom bindings
are preserved. Explicit per-binding or whole-profile Reset uses adapter defaults
while that adapter is connected. Auto-configure changes player assignment without
replacing mappings. Modified adapter firmware settings (such as swapping the stick
and D-pad) require matching custom bindings; the game never changes firmware.

Both ports may report identical identities. In particular, SDL2's generic macOS
backend supplies neither a serial nor a device path. Live ports remain separate,
and unplugging one does not renumber the other. Physical port order across a
complete unplug/relaunch is not guaranteed; use Assign players or Auto-configure
to establish the desired order. This limitation also applies to saved per-port
customizations when the OS changes enumeration order.

Select Rumble Pak for the player in Accessories and insert a real Rumble Pak in
the physical controller. The adapter firmware drives its motor as on/off, so the
strength slider can disable vibration but does not create continuously variable
physical intensity. Native SDL rumble is tried first, then a constant-force
fallback bound to that exact joystick. Effects expire within 250 ms without
refresh and stop on normal shutdown/disconnection. The fallback cannot supply
force feedback if macOS does not expose a working force-feedback service.

Controller Pak saves remain emulated files on the computer. Reading or writing a
physical Controller Pak through the adapter is not implemented. The manufacturer's
raw-access emulator plugins are a separate interface, not part of this recomp.

Validation: `scripts/test-n64-adapter.ps1` exercises the real SDL virtual joystick
mapping path; `scripts/test-controller-rumble.ps1` exercises production rumble code
with mocked SDL failure/lifecycle paths. Runtime profile tests cover persistence,
distinct identical devices, and device-specific reset. These tests do not establish
physical adapter compatibility, native macOS operation, or Rumble Pak delivery.

Protocol evidence:
- [Exact product and supported operating systems](https://www.raphnet-tech.com/products/dual_n64_to_usb_adapter/index.php)
- [Firmware button mapping](https://github.com/raphnet/gc_n64_usb-v3/blob/c681f648a5d66b0ed0fb8baa44fdae01c83be76c/mappings.c#L50-L69)
- [Firmware axis and force-feedback implementation](https://github.com/raphnet/gc_n64_usb-v3/blob/c681f648a5d66b0ed0fb8baa44fdae01c83be76c/usbpad.c)
- [Official USB device table](https://github.com/raphnet/gcn64tools/blob/12d5555e461255e3a268030a4db1fe6b670190ba/src/rntlib/raphnetadapter.c#L54-L96)
- [SDL2 macOS joystick backend](https://github.com/libsdl-org/SDL/blob/8acf7cfc0f88717e5c036f60f0314bfcafe39189/src/joystick/darwin/SDL_iokitjoystick.c)
