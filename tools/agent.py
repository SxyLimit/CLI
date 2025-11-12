import json
import sys


def send_message(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main():
    goal = ""
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            message = json.loads(raw)
        except json.JSONDecodeError:
            send_message({"type": "error", "message": "invalid json"})
            continue
        mtype = message.get("type")
        if mtype == "start":
            goal = message.get("goal", "")
            break
    answer = f"Agent session finished. Goal: {goal}" if goal else "Agent session finished."
    send_message({"type": "final", "answer": answer, "artifacts": []})


if __name__ == "__main__":
    main()
