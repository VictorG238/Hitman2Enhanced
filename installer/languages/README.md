# Installer message files not shipped with Inno Setup 6.7.3

Downloaded unchanged from the Inno Setup source repository (https://github.com/jrsoftware/issrc), which distributes
them under the Inno Setup license (free to use and redistribute):

| File | Source path | Ref |
|---|---|---|
| `ChineseSimplified.isl` | `Files/Languages/ChineseSimplified.isl` | `main` (2026-11-11; header: Inno Setup 6.5.0+) |
| `ChineseTraditional.isl` | `Files/Languages/ChineseTraditional.isl` | `main` (2026-11-11) |
| `Hindi.islu` | `Files/Languages/Unofficial/Hindi.islu` | tag `is-6_7_3` |

The Hindi translation predates Inno Setup 6: about 66 rare standard messages fall back to English (the compiler warns).
The project's own texts for every language are in `../lang/` and `../info-*.txt`.
