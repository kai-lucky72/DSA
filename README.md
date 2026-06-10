# Kigali City — Smart Parking Management System (C++ / DSA Practical)

A menu-driven, in-memory Smart Parking Management System for Kigali City.
No database is used — everything is managed with C++ data structures chosen
for the operations each task requires.

## Files

| File             | Purpose                                              |
|------------------|------------------------------------------------------|
| `main.cpp`        | Complete program (all classes + menu interface)         |
| `ARCHITECTURE.md` | System architecture & design (Mermaid diagrams)         |
| `test_input.txt`  | Scripted test inputs demonstrating every feature at once |
| `test_task1.txt`  | Focused test: slot configuration (Task 1)                |
| `test_task2.txt`  | Focused test: vehicle entry rules (Task 2)               |
| `test_task3.txt`  | Focused test: duration, fees, price updates (Task 3)     |
| `test_task4.txt`  | Focused test: exit, slot release, records (Task 4)       |
| `README.md`       | This file                                                |

Run any test file with:  `./smart_parking < test_taskN.txt`

## How to compile and run

Requires any C++17 compiler (g++ / MinGW / MSVC).

```bash
g++ -std=c++17 -Wall -Wextra -o smart_parking main.cpp
./smart_parking                  # interactive use
./smart_parking < test_input.txt # automated demo of all features
```

On Windows the executable is `smart_parking.exe`.

## Default parking tariffs (per started hour)

| Vehicle type | Rate            |
|--------------|-----------------|
| Motorcycle   | 500 RWF / hour  |
| Car          | 1,000 RWF / hour|
| Truck        | 2,000 RWF / hour|

- **Partial hours are billed as full hours**: 15 minutes → 1 hour; 1 h 20 min → 2 hours.
  Minimum charge is 1 hour.
- Fees are computed **only at exit**, using the tariff **active at exit time**.
- Tariffs can be updated at runtime (menu option 12); updates **never change
  completed transactions** because every receipt freezes its own rate copy.

## Menu options

| Option | Action |
|--------|--------|
| 1  | Add a parking slot (unique ID, vehicle type, zone) |
| 2  | Remove a slot (refused while occupied) |
| 3  | View all slots with status |
| 4  | View available slots only |
| 5  | Vehicle entry (unique plate, type, validated entry time, automatic slot allocation) |
| 6  | Vehicle exit — frees the slot, prints a fee receipt, records the transaction |
| 7  | List currently parked vehicles |
| 8  | Full history of one plate |
| 9  | All completed transactions |
| 10 | Daily revenue report (chronologically sorted) |
| 11 | View current tariffs |
| 12 | Update a tariff (controlled, validated) |
| 0  | Exit program |

At startup you can load 6 sample slots (M-01, M-02, C-01, C-02, C-03, T-01)
for a quick demo by answering `1`.

For times (entry/exit) you may **press ENTER to use the current system time**
or type a time manually as `YYYY-MM-DD HH:MM`.

## Vehicle plate number formats (enforced)

| Vehicle type | Format | Example |
|--------------|--------|---------|
| Car / Truck  | `RA` + 1 letter + 3 digits + 1 letter | `RAD123B` |
| Motorcycle   | `RB` + 3 digits + 1 letter            | `RB001A`  |

- The expected format is **always displayed as a reminder** before the user
  types the plate (the vehicle type is asked first for this reason).
- Lowercase is accepted (`rad123b` → `RAD123B`) and spaces are tolerated
  (`RB 001 A` → `RB001A`).
- A wrong plate produces a message explaining **exactly which rule was
  broken** (wrong prefix, wrong length, letter where a digit must be, ...)
  and the user is re-prompted.
- At exit / history lookup (type not yet known) the plate must match one of
  the two legal formats.

## Validation implemented

- **I/O validation** — every input is line-based: letters where numbers are
  expected, empty lines, out-of-range values, and EOF are all handled with a
  clear message and a re-prompt (no crashes, no infinite loops).
- **Date/time validation** — strict `YYYY-MM-DD HH:MM` format; month/day
  ranges, days-per-month, and **leap years** are checked (e.g. `2026-02-30`
  is rejected with the exact reason).
- **Timeline logic** —
  - a vehicle can never exit before it entered;
  - the system keeps a global event clock: every new entry/exit must be in
    chronological order with respect to the last recorded event;
  - duration arithmetic is exact across days/months/years (civil-calendar
    day-count algorithm), so overnight and multi-day parking bill correctly.
- **Business rules** — unique slot IDs, unique plates, no double parking,
  occupied slots cannot be deleted, graceful "no slot available" handling,
  positive bounded tariff updates.

## Data structures and justification

| Structure | Used for | Why |
|-----------|----------|-----|
| `unordered_map<string, ParkingSlot>` | slot registry | O(1) average lookup/insert/delete by unique Slot ID |
| `map<VehicleType, set<string>>` | free slots per type | non-linear BST; sorted free IDs → O(log n) allocation and release |
| `unordered_map<string, ActiveTicket>` | parked vehicles | O(1) "already parked?" check and exit lookup by plate |
| `vector<Transaction>` | completed transactions | append-only log, O(1) amortised insert, linear traversal for reports |
| `unordered_map<string, vector<size_t>>` | history index by plate | O(1) access to one vehicle's full history |
| `map<string, long long>` | daily revenue | keys sorted by date → report comes out chronological by in-order traversal |

## OOP design

- **Abstraction**: abstract base class `Vehicle` with pure virtual methods.
- **Inheritance**: `Motorcycle`, `Car`, `Truck` derive from `Vehicle`.
- **Polymorphism**: vehicles are created through a factory and used through
  `Vehicle*` virtual calls (`type()`, `typeName()`).
- **Encapsulation**: `DateTime`, `ParkingSlot`, `TariffManager` and
  `ParkingSystem` keep all data private behind validated methods.

## Demonstrated test run (from `test_input.txt`)

1. Load sample slots, list them.
2. Car `RAD123B` enters at 08:00 → allocated `C-01`.
3. Same plate tries to enter again → **rejected** (double parking).
4. Entry time `2026-02-30 10:00` → **rejected** (invalid calendar date).
5. Exit at 07:00 → **rejected** (exit before entry).
6. Exit at 09:20 → duration 1 h 20 min billed as **2 h × 1000 = 2,000 RWF**.
7. Car tariff updated to 1,500 RWF/h.
8. `CAR99` parks 10 min → billed **1 h × 1,500 = 1,500 RWF**; the earlier
   2,000 RWF record is unchanged (price update did not affect history).
9. Three cars fill all car slots; a fourth car is **gracefully refused**.
10. Non-numeric menu input `abc` → re-prompted.
11. Daily revenue report: `2026-06-10 : 3,500 RWF`.
