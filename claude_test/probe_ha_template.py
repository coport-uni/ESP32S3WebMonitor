# Renders the exact Jinja template main/ha_client.c builds against the real HA
# server, to validate the token and the Jinja syntax before burning a firmware
# build on them. Lifetime: delete once the device is polling successfully.
# Usage: python probe_ha_template.py
#
# Reads the URL and token straight out of ../sdkconfig, which is gitignored and
# is already where the firmware gets them — so the secret never reaches a
# command line, a repo file, or this script's source.
import json
import os
import re
import sys
import urllib.request
import urllib.error


def from_sdkconfig(key, default=None):
    path = os.path.join(os.path.dirname(__file__), "..", "sdkconfig")
    with open(path, encoding="utf-8") as f:
        m = re.search(r'^CONFIG_%s="(.*)"$' % re.escape(key), f.read(), re.M)
    if not m:
        if default is None:
            sys.exit("CONFIG_%s not set in sdkconfig" % key)
        return default
    return m.group(1)


HA = from_sdkconfig("HA_CLIENT_SERVER_URL").rstrip("/")
TOKEN = from_sdkconfig("HA_CLIENT_TOKEN")
LABEL = from_sdkconfig("HA_CLIENT_LABEL", "box3")
LIM = int(from_sdkconfig("HA_CLIENT_MAX_ENTITIES", "16")) + 1

TEMPLATE = (
    "{%% for e in label_entities('%s') %%}"
    "{%% set s = states[e] %%}"
    "{%% if s and loop.index <= %d %%}"
    "{{s.domain}}\t{{e}}\t{{s.name}}\t{{s.state}}\t"
    "{{s.attributes.get('unit_of_measurement','')}}\n"
    "{%% endif %%}{%% endfor %%}"
) % (LABEL, LIM)


def post(path, payload):
    body = json.dumps(payload).encode()
    req = urllib.request.Request(
        HA + path,
        data=body,
        headers={
            "Authorization": "Bearer " + TOKEN,
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
    except Exception as e:
        return -1, repr(e)


print("=== label:", LABEL, " cap+1:", LIM)
print("=== request body size:", len(json.dumps({"template": TEMPLATE})), "bytes")
status, text = post("/api/template", {"template": TEMPLATE})
print("=== /api/template status:", status)
print("=== response size:", len(text.encode()), "bytes (BUF_MAX on device is 8192)")
print("=== rendered TSV, one repr per line ===")
for line in text.splitlines():
    if line.strip():
        print(repr(line))
print("=== end TSV ===")

if status == 200 and not text.strip():
    print("!! empty: the label exists but has no entities, or does not exist yet.")
    status, text = post("/api/template", {"template": "{{ labels() }}"})
    print("=== labels defined on the server:", text.strip())
