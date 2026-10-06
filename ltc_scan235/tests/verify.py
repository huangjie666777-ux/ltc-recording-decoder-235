#!/usr/bin/env python3
"""Compare ltc_scan235 JSON output against gen_wav expectations."""
import json
import subprocess
import sys

def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        print("FAILED to run:", " ".join(cmd), file=sys.stderr)
        print(r.stderr, file=sys.stderr)
        sys.exit(1)
    return r.stdout

def decoded_timecodes(result):
    tcs = []
    for seg in result["segments"]:
        for f in seg["frames"]:
            tcs.append(f["timecode"])
    return tcs

def check(name, wav, meta, expect_segments=None, chunk=4096, extra=None):
    cmd = ["build/ltc_scan235", wav, "--chunk", str(chunk)] + (extra or [])
    result = json.loads(run(cmd))
    got = decoded_timecodes(result)
    want = meta["expected_timecodes"]
    ok = got == want
    if expect_segments is not None and result["segment_count"] != expect_segments:
        ok = False
        print(f"  segment_count {result['segment_count']} != {expect_segments}")
    if not ok:
        print(f"FAIL {name}", file=sys.stderr)
        if got != want:
            print(f"  decoded {len(got)} frames, expected {len(want)}", file=sys.stderr)
            for i, (g, w) in enumerate(zip(got, want)):
                if g != w:
                    print(f"  first mismatch at {i}: got {g} want {w}", file=sys.stderr)
                    break
        return False
    print(f"PASS {name} ({len(got)} frames, {result['segment_count']} segment(s))")
    return result

def main():
    gen = "build/gen_wav"
    all_ok = True

    def make(name, args):
        wav = f"build/test_{name}.wav"
        meta = json.loads(run([gen, "--out", wav] + args))
        return wav, meta

    # 1. Plain stereo, timecode on channel 1 (second channel), leading silence.
    wav, meta = make("basic", ["--frames", "250", "--channels", "2",
                               "--tc-channel", "1", "--silence", "0.5"])
    r = check("basic-stereo-ch1", wav, meta, expect_segments=1,
              extra=["--channel", "1"])
    all_ok &= bool(r)
    # Positions: first frame must start after the 0.5 s silence.
    if r:
        s0 = r["segments"][0]["start_sample"]
        if s0 < 24000:
            print("FAIL basic: first frame starts inside silence", file=sys.stderr)
            all_ok = False
        # 250 frames at 1920 samples each.
        seg = r["segments"][0]
        if seg["end_sample"] - seg["start_sample"] != 250 * 1920:
            print("FAIL basic: segment length mismatch", file=sys.stderr)
            all_ok = False
        if seg["end_reason"] != "signal_lost":
            print("FAIL basic: end_reason", seg["end_reason"], file=sys.stderr)
            all_ok = False

    # 2. Mono, inverted polarity, +2% speed.
    wav, meta = make("fast", ["--frames", "500", "--channels", "1",
                              "--invert", "--speed", "1.02"])
    all_ok &= bool(check("mono-invert-+2pct", wav, meta, expect_segments=1))

    # 3. -2% speed.
    wav, meta = make("slow", ["--frames", "500", "--channels", "1",
                              "--speed", "0.98"])
    all_ok &= bool(check("mono--2pct", wav, meta, expect_segments=1))

    # 4. Mid-frame recording start (first 37 bits missing).
    wav, meta = make("midframe", ["--frames", "100", "--start-offset-bits", "37"])
    all_ok &= bool(check("mid-frame-start", wav, meta, expect_segments=1))

    # 5. Silence gap in the middle -> two segments.
    wav, meta = make("gap", ["--frames", "200", "--gap-at", "100",
                             "--gap-len", "0.25"])
    r = check("silence-gap", wav, meta, expect_segments=2)
    all_ok &= bool(r)
    if r and r["segments"][0]["end_reason"] != "signal_lost":
        print("FAIL gap: reason", r["segments"][0]["end_reason"], file=sys.stderr)
        all_ok = False

    # 6. Timecode jump -> two segments.
    wav, meta = make("jump", ["--frames", "200", "--jump-at", "100",
                              "--jump-by", "100"])
    r = check("timecode-jump", wav, meta, expect_segments=2)
    all_ok &= bool(r)
    if r and r["segments"][0]["end_reason"] != "timecode_jump":
        print("FAIL jump: reason", r["segments"][0]["end_reason"], file=sys.stderr)
        all_ok = False

    # 7. Corrupted frame (illegal BCD) -> two segments, frame dropped.
    wav, meta = make("corrupt", ["--frames", "200", "--corrupt-at", "100"])
    r = check("corrupt-frame", wav, meta, expect_segments=2)
    all_ok &= bool(r)
    if r and r["segments"][0]["end_reason"] != "bad_frame":
        print("FAIL corrupt: reason", r["segments"][0]["end_reason"], file=sys.stderr)
        all_ok = False

    # 8. Drop-frame flag set -> frame rejected.
    wav, meta = make("drop", ["--frames", "100", "--dropflag-at", "50"])
    all_ok &= bool(check("drop-frame-flag", wav, meta, expect_segments=2))

    # 9. Midnight wrap stays in one segment.
    wav, meta = make("midnight", ["--start", "23:59:59:20", "--frames", "30"])
    all_ok &= bool(check("midnight-wrap", wav, meta, expect_segments=1))

    # 10. Chunk-size independence: results must be identical for
    #     chunk sizes 1, 7, 4096, 65536.
    wav, meta = make("chunk", ["--frames", "120", "--gap-at", "60",
                               "--gap-len", "0.1", "--speed", "1.015"])
    base = None
    for cs in (1, 7, 4096, 65536):
        r = json.loads(run(["build/ltc_scan235", wav, "--chunk", str(cs)]))
        key = json.dumps(r["segments"], sort_keys=True)
        if base is None:
            base = key
        elif key != base:
            print(f"FAIL chunk-independence at chunk={cs}", file=sys.stderr)
            all_ok = False
            break
    else:
        print("PASS chunk-independence (1/7/4096/65536 identical)")

    print("ALL PASS" if all_ok else "SOME TESTS FAILED")
    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()

