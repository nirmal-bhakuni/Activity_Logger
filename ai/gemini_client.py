"""
gemini_client.py - a small helper that sends ONE request to the Gemini API and returns parsed JSON.

Uses only the standard library (no pip install). The API key is sent in a request header,
never in the URL, and is never printed.
"""
import configparser
import json
import os
import re
import time
import urllib.error
import urllib.request

BASE = "https://generativelanguage.googleapis.com/v1beta"
RETRY_CODES = {429, 500, 502, 503, 504}     # busy / rate limited: worth trying again
WAIT_BETWEEN_TRIES = [3, 8]                 # seconds, so at most 3 attempts


class GeminiError(Exception):
    pass


def load_settings(path="config.ini"):
    """Key from the GEMINI_API_KEY environment variable first, then [gemini] in config.ini."""
    cfg = configparser.ConfigParser(interpolation=None)   # interpolation=None: a % in a password is safe
    cfg.read(path)
    key = os.environ.get("GEMINI_API_KEY", "").strip() or cfg.get("gemini", "api_key", fallback="").strip()
    model = cfg.get("gemini", "model", fallback="").strip()
    if model.startswith("models/"):
        model = model[len("models/"):]
    return key, model


def _post(model, key, body, timeout=60):
    data = json.dumps(body).encode("utf-8")
    url = "%s/models/%s:generateContent" % (BASE, model)
    last_error = None
    for attempt in range(len(WAIT_BETWEEN_TRIES) + 1):
        req = urllib.request.Request(
            url, data=data, method="POST",
            headers={"x-goog-api-key": key, "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            text = e.read().decode("utf-8", errors="replace")[:400]
            last_error = GeminiError("Gemini returned HTTP %d: %s" % (e.code, text))
            if e.code not in RETRY_CODES:
                raise last_error
        except urllib.error.URLError as e:
            last_error = GeminiError("Could not reach Gemini (check the internet connection): %s" % e.reason)
        if attempt < len(WAIT_BETWEEN_TRIES):
            time.sleep(WAIT_BETWEEN_TRIES[attempt])
    raise last_error


def _extract_json(text):
    text = text.strip()
    text = re.sub(r"^```(?:json)?\s*|\s*```$", "", text)      # remove ```json fences if the model added them
    try:
        return json.loads(text)
    except ValueError:
        raise GeminiError("Gemini did not return valid JSON. Start of its answer: %r" % text[:300])


def generate_json(system_text, user_text, model, key, temperature=0.3, max_output_tokens=2000):
    """Returns (parsed_json, usage_metadata_dict)."""
    body = {
        "systemInstruction": {"parts": [{"text": system_text}]},
        "contents": [{"role": "user", "parts": [{"text": user_text}]}],
        "generationConfig": {
            "responseMimeType": "application/json",
            "temperature": temperature,
            "maxOutputTokens": max_output_tokens,
        },
    }
    reply = _post(model, key, body)

    candidates = reply.get("candidates") or []
    if not candidates:
        raise GeminiError("Gemini gave no answer. Details: %s" % json.dumps(reply.get("promptFeedback", reply))[:400])
    cand = candidates[0]
    if cand.get("finishReason") == "MAX_TOKENS":
        raise GeminiError("Gemini's answer was cut off. Raise max_output_tokens in gemini_client.generate_json().")

    parts = (cand.get("content") or {}).get("parts") or []
    text = "".join(p.get("text", "") for p in parts if not p.get("thought"))    # skip any 'thinking' parts
    if not text.strip():
        raise GeminiError("Gemini's answer was empty (finishReason=%s)." % cand.get("finishReason"))
    return _extract_json(text), reply.get("usageMetadata", {})
