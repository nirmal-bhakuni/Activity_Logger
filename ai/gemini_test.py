"""
gemini_test.py - checks that your Gemini API key works. Run this BEFORE building anything else.

What it does:
  1. Reads the key from the GEMINI_API_KEY environment variable, or from config.ini ([gemini] api_key=...)
  2. Lists the models your key can use (model names change often, so we ask Google instead of guessing)
  3. If config.ini has [gemini] model=..., sends one tiny test question to that model

Run from the project folder:   python ai/gemini_test.py
Needs only standard Python 3 (no pip install).
"""
import configparser
import json
import os
import sys
import urllib.error
import urllib.request

BASE = "https://generativelanguage.googleapis.com/v1beta"


def load_settings():
    cfg = configparser.ConfigParser(interpolation=None)   # interpolation=None: passwords with % are safe
    cfg.read("config.ini")
    key = os.environ.get("GEMINI_API_KEY", "").strip() or cfg.get("gemini", "api_key", fallback="").strip()
    model = cfg.get("gemini", "model", fallback="").strip()
    if model.startswith("models/"):
        model = model[len("models/"):]
    return key, model


def call_api(path, key, body=None):
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        BASE + path,
        data=data,
        headers={"x-goog-api-key": key, "Content-Type": "application/json"},
        method="GET" if body is None else "POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        print("The API returned HTTP error", e.code)
        print(e.read().decode("utf-8", errors="replace")[:600])
        sys.exit(1)
    except urllib.error.URLError as e:
        print("Could not reach the API (check your internet connection):", e.reason)
        sys.exit(1)


def main():
    key, model = load_settings()
    if not key:
        print("No API key found.")
        print("Set the GEMINI_API_KEY environment variable, or add this to config.ini:")
        print("  [gemini]")
        print("  api_key=YOUR_KEY_HERE")
        sys.exit(1)
    print("Key found (length %d). Not printing it." % len(key))

    print("\nModels your key can use for text generation:")
    result = call_api("/models?pageSize=200", key)
    found = []
    for m in result.get("models", []):
        if "generateContent" in m.get("supportedGenerationMethods", []):
            found.append(m["name"].replace("models/", "", 1))
    for name in sorted(found):
        print("  ", name)
    if not found:
        print("  (none listed)")

    if not model:
        print("\nNext: pick one name above that has 'flash' in it and add it to config.ini:")
        print("  [gemini]")
        print("  api_key=...")
        print("  model=THE_NAME_YOU_PICKED")
        print("then run this script again.")
        return

    print("\nSending a test question to model '%s' ..." % model)
    body = {"contents": [{"parts": [{"text": "Reply with the single word: OK"}]}]}
    reply = call_api("/models/%s:generateContent" % model, key, body)
    try:
        print("Model replied:", reply["candidates"][0]["content"]["parts"][0]["text"].strip())
        print("\nSUCCESS. The key and the model both work.")
    except (KeyError, IndexError):
        print("Unexpected reply format:")
        print(json.dumps(reply, indent=2)[:800])


if __name__ == "__main__":
    main()