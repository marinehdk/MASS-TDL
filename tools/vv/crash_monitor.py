#!/usr/bin/env python3
"""1h crash-free monitor — DoD #11.
Usage: python tools/vv/crash_monitor.py --duration 3600
"""
from __future__ import annotations
import argparse, json, subprocess, sys, time
from pathlib import Path

RSS_LIMIT_MB = 4096
VETO_CONSECUTIVE_LIMIT = 3

def get_process_rss_mb(process_names: list[str]) -> dict[str, float]:
    result: dict[str, float] = {}
    for name in process_names:
        try:
            out = subprocess.run(["bash", "-c", f"ps aux | grep '{name}' | grep -v grep | awk '{{sum+=$6}} END {{print sum}}'"],
                capture_output=True, text=True, timeout=5).stdout.strip()
            rss_kb = int(out) if out.isdigit() else 0
            result[name] = round(rss_kb / 1024, 1)
        except Exception:
            result[name] = -1.0
    return result

def get_veto_count_from_ros2() -> int:
    try:
        out = subprocess.run(["ros2","topic","echo","/sil/asdr_event","--spin-time","4.5","--no-arr"],
            capture_output=True,text=True,timeout=6).stdout
        return out.count("verdict: 3")
    except Exception:
        return 0

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--duration",type=float,default=3600)
    ap.add_argument("--procs",default="sil_orchestrator,behavior_arbiter_node,safety_supervisor_node")
    ap.add_argument("--output",default="evidence/1h_crash_free.jsonl")
    args = ap.parse_args()
    procs = [p.strip() for p in args.procs.split(",")]
    out_path = Path(args.output); out_path.parent.mkdir(parents=True,exist_ok=True)
    start = time.time(); deadline = start + args.duration
    consecutive_veto = 0; status = "OK"
    with open(out_path,"w") as f:
        while time.time() < deadline:
            t = time.time()
            rss = get_process_rss_mb(procs)
            veto_count = get_veto_count_from_ros2()
            total_rss = sum(v for v in rss.values() if v > 0)
            if total_rss > RSS_LIMIT_MB:
                status = f"FAIL:RSS_EXCEEDED:{total_rss:.0f}MB"; break
            if veto_count >= VETO_CONSECUTIVE_LIMIT:
                consecutive_veto += 1
                if consecutive_veto >= VETO_CONSECUTIVE_LIMIT:
                    status = f"FAIL:VETO_STORM:{consecutive_veto}"; break
            else:
                consecutive_veto = 0
            for proc_name in procs:
                try:
                    subprocess.run(["pgrep","-f",proc_name],check=True,capture_output=True,timeout=2)
                except subprocess.CalledProcessError:
                    status = f"FAIL:CRASH:{proc_name}"; break
            if status != "OK": break
            entry = {"timestamp":round(t-start,1),"rss_mb":rss,"veto_count":veto_count,
                     "status":status,"elapsed_pct":round((t-start)/args.duration*100,1)}
            f.write(json.dumps(entry)+"\n"); f.flush()
            time.sleep(max(0, 5.0-(time.time()-t)))
        final = {"timestamp":round(time.time()-start,1),"rss_mb":get_process_rss_mb(procs),
                 "veto_count":0,"status":status,"elapsed_pct":100.0}
        f.write(json.dumps(final)+"\n")
    print(f"\n[DoD #11] Final status: {status}")
    return 0 if status == "OK" else 1

if __name__ == "__main__":
    sys.exit(main())
