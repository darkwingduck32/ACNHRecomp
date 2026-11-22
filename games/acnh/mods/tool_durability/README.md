# Tool durability (RomFS)

Raises all 23 breakable tool profiles to 65,535 uses, including flimsy, regular,
golden and other tool variants. Existing wear still counts toward this limit.
This is practical extended durability, not disabled decay or literal infinity.

Uses the approach demonstrated by [Weaselgrease's Tool Durability Multipliers](https://gamebanana.com/mods/342961).
The generator edits the user's current table rather than installing the example's
older table, preserving newer equipment and all unrelated fields.

Generate from your own extracted game files (no game data is included here):

```powershell
private/venv/Scripts/python.exe tools/make_tool_durability_mod.py --romfs private/game/romfs --out private/tool_durability
```

Import `private/tool_durability.zip` using the launcher's Mods manager, enable it,
and restart the player. Alternatively copy the generated folder to
`<game folder>/mods/tool_durability` and enable `tool_durability`.
The overlay replaces only `romfs/Bcsv/ItemOutfitInfo.bcsv`; another mod replacing
that file must be merged or given lower priority. No player rebuild is required.

Disable the mod and restart to restore stock limits. Tools whose accumulated wear
exceeds the stock limit may break when used after disabling it.

Generated against ACNH 3.0.3 and checked for byte preservation outside the 23
durability fields. In-game lifetime behavior has not been verified locally.
