# 5. A GUI that never waits

A frame has about 16 ms. If anything in it waits (for the network, for the PLC, for a file on a slow disk), the whole window freezes: no redraw, no reaction to the mouse. Online mode talks to a PLC over TCP, where a reply can take many milliseconds, or never come. This note is about how the editor gets values from the PLC without ever waiting for one.

---

## The rule

**The UI thread never makes a network call.** All of them happen on one background thread, owned by `online::Session` (`src/online/session.hpp`). The UI and that thread share a few variables, protected by one mutex, and the UI only ever holds the mutex for an instant.

```
UI thread (every frame)                    session thread (about 10 times a second)
───────────────────────                    ────────────────────────────────────────
watch({"start","t1.Q"})  ─┐                take the commands and the watch list (lock, swap, unlock)
send("WRITE start TRUE") ─┼─► mutex ◄──┐   send commands      ─┐
value("t1.Q")            ─┘            │   READ start t1.Q     ├─ network, no lock held
                                       │   STATUS, FORCES     ─┘
                                       └── store the results (lock, assign, unlock)
```

---

## Copy under the lock, work outside it

`Session::exchange()` is the heart of it:

1. **Lock, copy, unlock.** Take the queued commands (`commands.swap(commands_)`) and copy the watch list.
2. **Talk to the PLC, with no lock held.** Send each command, read the watched variables 64 at a time, read the status and the forces. This is where the waiting happens, and the UI isn't affected: it can lock the mutex and read the previous values meanwhile.
3. **Lock, store, unlock.** Replace the values, forces and status with the new ones.

The mistake this avoids is holding the mutex during the network calls. It would be correct (no data race) and the UI would still freeze, because the UI's next `value()` call would wait for the mutex, and the mutex would be waiting for the network.

The UI-side functions are all of this shape:

```cpp
std::optional<std::string> Session::value(std::string_view path) const {
    std::lock_guard lock(mutex_);
    ...find it in values_, copy it out...
}
```

They return **copies**, never references into the session's maps: a reference would be read after the lock is released, while the session thread might be replacing the map.

---

## Waking up early

The thread works in rounds, about every 100 ms. But a click on **Set TRUE** shouldn't wait up to 100 ms to be sent. So the wait is a `std::condition_variable` with a timeout:

```cpp
wake_.wait_for(lock, period_, [this] { return stop_ || !commands_.empty(); });
```

It returns after 100 ms, or **at once** when a command is queued (`send` notifies it) or the session is being destroyed. The predicate also handles the case where the command arrived just *before* the thread started waiting, which a plain "wait for a notification" would miss.

---

## Results come back later, and that's fine

Some actions need an answer: **Download** must know whether soft_plc accepted the new version. The UI can't wait for it, so it's a two-part conversation:

1. The click calls `session_->load(source)`, which only queues it, and the window remembers the project it sent (`pendingDownload_`). The button is disabled meanwhile.
2. Every frame, `checkDownload()` asks the session for the last load's result. When one with a new number appears, it either records the project as downloaded and shows what was kept, or shows why it was refused.

Each load result has a **number** (1, 2, 3, …), so the window can tell a new answer from one it has already handled, without the session having to know who's asking.

The general pattern: the UI thread **starts** things and **polls** for results each frame. That fits immediate mode perfectly (note 3), since the UI runs every frame anyway.

---

## Lifetimes across threads

The session thread uses the session's members, so it must start after they exist and stop before they're destroyed:
- `std::thread thread_` is the **last** member, and it's started at the end of the constructor, when everything else is ready.
- The destructor sets `stop_`, notifies the condition variable, and **joins** the thread before any member is destroyed.

And the window destroys the session **before** the simulator it's connected to (`goOffline` resets the session first). If the simulator went first, its server would close the connection, the session thread would see the connection lost, and the result would still be correct; but stopping the client first is the order that never makes anything handle an error it didn't need to.

---

## What a background thread can't fix

The values the UI shows are always **a little old**: up to a round (100 ms) plus the PLC's cycle. For watching a process that's fine, and it's how every PLC monitoring tool works. It also means that after **Set TRUE**, the old value is shown for a moment. The UI doesn't pretend otherwise by showing the requested value before the PLC confirms it: it shows what the PLC says.

---

## Review questions

**1.** What would go wrong if `exchange()` held the mutex while sending `READ` to the PLC?
<details><summary>Answer</summary>

Nothing would be corrupted, but the UI would freeze: every frame calls `value()`, which needs the mutex, so it would wait for the network round trip each time the session was reading. Copying under the lock and doing the network work without it keeps the UI's waits to an instant.
</details>

**2.** Why does `Session::value` return a copy of the string, not a reference to it?
<details><summary>Answer</summary>

A reference would be used after the lock is released, while the session thread may be replacing `values_` with a new map: the reference would point into freed memory. A copy made under the lock is the caller's own.
</details>

**3.** Why does the condition variable wait have a predicate, instead of just waiting for a notification or the timeout?
<details><summary>Answer</summary>

If a command was queued just before the thread started waiting, its notification has already happened and would be missed: the thread would sleep the full 100 ms. The predicate checks the queue before sleeping (and after waking, which also covers spurious wake-ups).
</details>

**4.** How does the window know that a Download's answer is new, and not one it has already shown?
<details><summary>Answer</summary>

Each load result carries a number that increases with every load. The window remembers the last number it handled (`loadsSeen_`) and only acts on a result with a higher one.
</details>
