# ziru-extra

Server responses written for this port (not taken from Ziru's APK). `build_ipa.py` copies `responses/*` into
`PESmobile.app/ziru/responses/` after Ziru's files, so a file here adds a missing command or overrides Ziru's.

- `CMD_GET_AGE_GATE_REQUIREMENTS.json`: new in client 11.1.1 (Ziru's server targets 11.0.1 and has no response).
  Fields read by `CmdGetAgeGateRequirementsResult::UnPackJson` (0x103b38128): `result`, `is_eula_country`
  (YES/NO, compared with `CommandCamYesno` literal 1), `country_age_kind` (NONE / BIRTH_DATE / BIRTH_YEAR_MONTH,
  `EMenuEulaCountryAgeKind`). Values match Ziru's `get_user_eula_info.json` (`eula_country_age_kind: BIRTH_YEAR_MONTH`).
  With `is_eula_country: YES` QuitCommand keeps the stored birthday; `NO` clears it and reruns the birthday flow.
