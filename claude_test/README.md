# claude_test/

Debug, exploratory, and throwaway scratch files for this project.

Per `CLAUDE.md` section 3, anything under here is exempt from the 80-column limit and from the "no magic numbers" rule — provided each file opens with a one-line intent comment.

Production code lives in `main/` and (future) `components/`. If a debug script proves useful, fold the relevant logic back into production code and delete the scratch file.

## Index

| File | Purpose | Lessons learned |
|------|---------|-----------------|
| `probe_ha_template.py` | Renders the exact Jinja template `main/ha_client.c` builds against the live HA server, so the token, the Jinja syntax and the label filter are proven before a firmware build is spent on them. Reads URL + token from the gitignored `../sdkconfig` — the same place the firmware gets them — so no secret reaches a command line or a tracked file. | Caught the whole design being wrong before the first flash. `/api/template` renders in ~600 B where `/api/states` would have been hundreds of KB. Also: passing the token as `argv[1]` is rejected by the permission classifier — read it from `sdkconfig` instead. |
| `probe_ha_attrs.py` | Dumps every switch/sensor with the attributes that might separate a real device (`switch.tapo_p1`) from an integration's own config toggle (`switch.tapo_p1_led`): `entity_category`, `device_class`, full attrs, `is_hidden_entity`, `area_name`, `labels`. | **HA exposes no way to tell them apart.** `entity_category` is not reachable from a template (`NOT_DEFINED`), every switch's `device_class` is `NONE`, attributes hold only `friendly_name`, and config entities inherit their parent device's area (all "Living Room"). This killed the domain-sweep design and forced the label filter in `CONFIG_HA_CLIENT_LABEL`. Of 18 switches only 3 were real plugs. |
| `apply_ha_labels.py` | Puts the `CONFIG_HA_CLIENT_LABEL` label on the entities the screen should show. Labels live in the entity registry, which REST does not expose — only the WebSocket API does. Defaults to a dry run; `--apply` writes. | `config/entity_registry/update` takes `labels` as a list of **label_ids**, not names. After writing, a `render_template` on the *same* WebSocket connection still returns empty — the registry cache is stale for that connection. A fresh REST call sees the labels immediately, so verify over REST, not over the connection that wrote. |
| `read_boot_log.py` | Captures the serial boot log for N seconds and exits. | `idf.py monitor` is interactive and hangs a non-interactive session forever waiting on `Ctrl+]`. Use this instead. |
