This beta hotfix addresses feedback from the YACP 1.7.0 dictionary test.

### Fixed

- Re-selecting Justify after another paragraph alignment now keeps visible capped spacing instead of falling back to
  left-aligned spacing on lines whose full justification would create oversized gaps.
- Reading Rhythm now uses compact daily durations, distributes all available width across the seven recent days, and
  keeps the current-day frame clear of its label and duration.
- Fixed EPUB pages rendering replacement glyphs when a custom SD-card font has a negative hashed ID.
- Fixed custom SD-card fonts in dictionary word selection and definition screens.
- Fixed Cambridge and other XDXF StarDict dictionaries being reported as missing under `/.dictionaries/<folder>`.

### Simulator previews

<table>
  <tr>
    <td align="center">
      <img src="https://raw.githubusercontent.com/Sichroteph/YACP/release/1.7.0-yacp.4/docs/images/yacp/media/paragraph-justify.png"
           alt="EPUB paragraph alignment with Justify enabled on the X3 simulator"
           width="264">
    </td>
    <td align="center">
      <img src="https://raw.githubusercontent.com/Sichroteph/YACP/release/1.7.0-yacp.4/docs/images/yacp/media/reading-rhythm.png"
           alt="Reading Rhythm with compact daily durations on the X3 simulator"
           width="264">
    </td>
  </tr>
  <tr>
    <td align="center">Justify rendering</td>
    <td align="center">Reading Rhythm</td>
  </tr>
</table>
