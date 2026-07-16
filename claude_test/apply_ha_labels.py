# One-off setup: puts the CONFIG_HA_CLIENT_LABEL label on the entities the
# BOX-3 screen should show. Labels live in the entity registry, which the REST
# API does not expose — only the WebSocket API does, hence this script rather
# than a curl. Lifetime: delete once the label set is stable; after that,
# labelling is a two-click job in the HA UI and needs no reflash.
#
# Usage: python apply_ha_labels.py [--apply]
#        Without --apply it only reports what it would change.
#        Reads url + token from the gitignored ../sdkconfig.
import asyncio
import json
import os
import re
import sys

import websockets

TARGETS = [
    # Arduino Uno Q, pin 3 RGB.
    "switch.uno_q_mcu_uno_q_led3_r",
    "switch.uno_q_mcu_uno_q_led3_g",
    "switch.uno_q_mcu_uno_q_led3_b",
    # The Tapo plugs themselves. Deliberately NOT their _led /
    # _auto_off_enabled / _auto_update_enabled siblings, which share the
    # domain, the area and the (absent) device_class and are the reason a
    # plain domain sweep was unusable here.
    "switch.tapo_p1",
    "switch.tapo_p2",
    "switch.tapo_p3",
    # Per-plug current consumption (device_class: power).
    "sensor.tapo_p1_current_consumption",
    "sensor.tapo_p2_current_consumption",
    "sensor.tapo_p3_current_consumption",
]


def from_sdkconfig(key, default=None):
    path = os.path.join(os.path.dirname(__file__), "..", "sdkconfig")
    with open(path, encoding="utf-8") as f:
        m = re.search(r'^CONFIG_%s="(.*)"$' % re.escape(key), f.read(), re.M)
    if not m:
        if default is None:
            sys.exit("CONFIG_%s not set in sdkconfig" % key)
        return default
    return m.group(1)


BASE = from_sdkconfig("HA_CLIENT_SERVER_URL").rstrip("/")
TOKEN = from_sdkconfig("HA_CLIENT_TOKEN")
LABEL = from_sdkconfig("HA_CLIENT_LABEL", "box3")
WS = BASE.replace("http://", "ws://").replace("https://", "wss://") + "/api/websocket"
APPLY = "--apply" in sys.argv


class Client:
    def __init__(self, ws):
        self.ws = ws
        self._id = 0

    async def cmd(self, type_, **kw):
        self._id += 1
        await self.ws.send(json.dumps({"id": self._id, "type": type_, **kw}))
        while True:
            msg = json.loads(await self.ws.recv())
            if msg.get("id") == self._id and msg.get("type") == "result":
                if not msg.get("success"):
                    raise RuntimeError("%s failed: %s" % (type_, msg.get("error")))
                return msg.get("result")


async def main():
    async with websockets.connect(WS, max_size=8 * 1024 * 1024) as ws:
        hello = json.loads(await ws.recv())
        assert hello["type"] == "auth_required", hello
        await ws.send(json.dumps({"type": "auth", "access_token": TOKEN}))
        ok = json.loads(await ws.recv())
        if ok.get("type") != "auth_ok":
            sys.exit("auth failed: %r" % ok)
        print("auth ok, HA %s" % ok.get("ha_version"))

        c = Client(ws)

        labels = await c.cmd("config/label_registry/list")
        match = next((l for l in labels if l["name"] == LABEL), None)
        if match:
            label_id = match["label_id"]
            print("label %r already exists (id=%s)" % (LABEL, label_id))
        elif APPLY:
            created = await c.cmd("config/label_registry/create", name=LABEL)
            label_id = created["label_id"]
            print("created label %r (id=%s)" % (LABEL, label_id))
        else:
            label_id = "<would-create>"
            print("label %r does not exist; would create it" % LABEL)

        entities = await c.cmd("config/entity_registry/list")
        by_id = {e["entity_id"]: e for e in entities}

        for eid in TARGETS:
            ent = by_id.get(eid)
            if not ent:
                print("  MISSING  %s  (not in the entity registry)" % eid)
                continue
            current = list(ent.get("labels") or [])
            if label_id in current:
                print("  ok       %s  (already labelled)" % eid)
                continue
            if not APPLY:
                print("  would    %s  labels %s -> %s"
                      % (eid, current, current + [LABEL]))
                continue
            await c.cmd("config/entity_registry/update",
                        entity_id=eid, labels=current + [label_id])
            print("  labelled %s" % eid)

        # Read back through the same path the firmware uses, so this verifies
        # the template rather than just the registry write.
        tmpl = ("{%% for e in label_entities('%s') %%}{%% set s = states[e] %%}"
                "{{s.domain}}|{{e}}|{{s.name}}|{{s.state}}|"
                "{{s.attributes.get('unit_of_measurement','')}}\n"
                "{%% endfor %%}" % LABEL)
        rendered = await c.cmd("render_template", template=tmpl)
        print("\n=== label_entities('%s') now renders ===" % LABEL)
        print(rendered if rendered else "(empty)")


asyncio.run(main())
