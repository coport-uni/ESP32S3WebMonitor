# Dumps every switch/sensor entity with the attributes that might discriminate
# a real controllable device (switch.tapo_p1) from an integration's config
# toggle (switch.tapo_p1_led). Answers: can the firmware auto-filter these
# without hardcoding entity ids? Lifetime: delete once the filter is chosen.
# Usage: python probe_ha_attrs.py   (reads url+token from ../sdkconfig)
import json
import os
import re
import sys
import urllib.request
import urllib.error


def from_sdkconfig(key):
    path = os.path.join(os.path.dirname(__file__), "..", "sdkconfig")
    with open(path, encoding="utf-8") as f:
        m = re.search(r'^CONFIG_%s="(.*)"$' % re.escape(key), f.read(), re.M)
    if not m:
        sys.exit("CONFIG_%s not set in sdkconfig" % key)
    return m.group(1)


HA = from_sdkconfig("HA_CLIENT_SERVER_URL").rstrip("/")
TOKEN = from_sdkconfig("HA_CLIENT_TOKEN")


def post(path, payload):
    req = urllib.request.Request(
        HA + path,
        data=json.dumps(payload).encode(),
        headers={"Authorization": "Bearer " + TOKEN, "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
    except Exception as e:
        return -1, repr(e)


def show(title, tmpl):
    status, text = post("/api/template", {"template": tmpl})
    print("\n=== %s (status %s) ===" % (title, status))
    print(text.strip()[:2000])


# 1. Is entity_category reachable from a template at all? If it is,
#    "entity_category is none" is the textbook way to drop config/diagnostic
#    entities.
show("switch entity_category", (
    "{% for s in states.switch %}{{ s.entity_id }} -> "
    "{{ s.entity_category if s.entity_category is defined else 'NOT_DEFINED' }}\n"
    "{% endfor %}"
))

# 2. device_class per switch. Real Tapo plugs are expected to be 'outlet'
#    while the LED/auto-off toggles should differ.
show("switch device_class", (
    "{% for s in states.switch %}{{ s.entity_id }} -> "
    "{{ s.attributes.get('device_class', 'NONE') }}\n"
    "{% endfor %}"
))

# 3. Full attribute dump for one real plug vs one config toggle, to spot any
#    other discriminator.
show("attrs: switch.tapo_p1 (real plug)", "{{ states.switch.tapo_p1.attributes }}")
show("attrs: switch.tapo_p1_led (config toggle)", "{{ states.switch.tapo_p1_led.attributes }}")

# 4. Does the template engine expose the entity registry helpers?
show("is_hidden_entity / labels", (
    "hidden_p1={{ is_hidden_entity('switch.tapo_p1') }}\n"
    "hidden_led={{ is_hidden_entity('switch.tapo_p1_led') }}\n"
    "labels_p1={{ labels('switch.tapo_p1') }}\n"
    "areas={{ areas() }}\n"
    "area_p1={{ area_name('switch.tapo_p1') }}\n"
))

# 5. Sensor device_class census with counts, to size the allow-list.
show("sensor device_class counts", (
    "{% set ns = namespace(rows=[]) %}"
    "{% for s in states.sensor %}"
    "{% set dc = s.attributes.get('device_class', 'NONE') %}"
    "{% set ns.rows = ns.rows + [dc] %}"
    "{% endfor %}"
    "{% for dc in ns.rows | unique | list %}{{ dc }}={{ ns.rows | select('eq', dc) | list | count }} {% endfor %}"
))

# 6. Area per switch. Config toggles inherit their parent device's area, so
#    this probably will NOT separate tapo_p1 from tapo_p1_led — but it should
#    separate the Tapo plugs from the Uno Q LEDs, and it is the only registry
#    facet templates expose.
show("switch area", (
    "{% for s in states.switch %}{{ s.entity_id }} -> "
    "{{ area_name(s.entity_id) }}\n{% endfor %}"
))

# 7. area_entities() per area — if areas are clean, the firmware could poll
#    one area instead of a whole domain.
show("area_entities", (
    "{% for a in areas() %}{{ a }}: {{ area_entities(a) | join(', ') }}\n{% endfor %}"
))

# 8. Do labels exist anywhere yet? An empty result means the label route needs
#    HA-side setup before it can be used.
show("all labels", "{{ labels() }}")
