# SPDX-License-Identifier: MIT
"""Parts sharing one instance, against a running Oscairn (RFC 0021).

Drives Oscairn's JSON-RPC test server (built from the Oscairn repository) with this example's
plugin: two parts whose instruments are this plugin share one instance, each with its own sound,
MIDI channel and output bus. Checked through graph.get's per-node `shared` field.

Usage: oscairn-sharing.py oscairn-json-rpc-test PATH/TO/oscairn-example-instance-sharing.clap
"""
import json
import subprocess
import sys

rpc_id = 0


def start():
    return subprocess.Popen([sys.argv[1], sys.argv[2], "--stdio"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True, encoding="utf-8")


def rpc(method, params=None, expect_error=False):
    global rpc_id
    rpc_id += 1
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": rpc_id, "method": method,
                             "params": params or {}}) + "\n")
    p.stdin.flush()
    response = json.loads(p.stdout.readline())
    assert ("error" in response) == expect_error, response
    return response


def drain():
    p.stdin.write("!drain\n")
    p.stdin.flush()
    assert json.loads(p.stdout.readline()) == {}


def shared_of(nodes, name):
    node = next(n for n in nodes if n["name"] == name)
    return node["id"], node["shared"]


p = start()
try:
    # The server opens with one node of the plugin, "synth" (its argv[1]). Give it a part,
    # then a second part with a second node of the same plugin: the pair shares one instance,
    # the second node taking a new sound, channel and output bus of the first's instance.
    part_one = rpc("parts.add", {"name": "Left"})["result"]["part"]
    rpc("parts.insert", {"part": part_one, "index": 0, "node": "synth"})
    rpc("graph.commit")
    drain()

    second = rpc("graph.add", {"name": "Right", "path": sys.argv[2]})["result"]["node"]
    part_two = rpc("parts.add", {"name": "Right part"})["result"]["part"]
    rpc("parts.insert", {"part": part_two, "index": 0, "node": second})
    rpc("graph.commit")
    drain()

    nodes = rpc("graph.get")["result"]["nodes"]
    left, shared_left = shared_of(nodes, "synth")
    right, shared_right = shared_of(nodes, "Right")
    assert shared_left, f"the first node reports no share: {shared_left}"
    assert shared_right, f"the second node reports no share: {shared_right}"
    assert shared_left["engine"] == shared_right["engine"] == left, \
        f"the two do not share one engine: {shared_left} vs {shared_right}"
    assert shared_left["channel"] != shared_right["channel"], \
        f"both parts on one channel: {shared_left} vs {shared_right}"
    assert shared_left["bus"] != shared_right["bus"], \
        f"both parts on one output bus: {shared_left} vs {shared_right}"

    # Each part's sound is its own: editing one part's parameters (sent on its channel) moves
    # its sound alone, and the share keeps its bus.
    rpc("params.set", {"values": [{"node": right, "param": 1, "value": 0.9},
                                 {"node": right, "param": 2, "value": 5.0}]})
    drain()
    nodes = rpc("graph.get")["result"]["nodes"]
    assert shared_of(nodes, "Right")[1] == shared_right, "the share moved after a parameter edit"
    by_name = {n["name"]: n["id"] for n in nodes}
    def pitch_of(name):
        got = rpc("params.get", {"values": [{"node": by_name[name], "param": 2}]})["result"]["values"]
        return got[0]["value"]
    assert pitch_of("Right") == 5.0, "the second part lost its pitch"
    assert pitch_of("synth") != 5.0, "the first part took the second's pitch"

    print("ok: two parts share one instance, each its channel and bus")
finally:
    p.stdin.close()
    p.wait(timeout=30)
sys.exit(0)
