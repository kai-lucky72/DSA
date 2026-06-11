# System Architecture — Kigali Smart Parking Management System

All diagrams are written in **Mermaid**. Paste any block into
[mermaid.live](https://mermaid.live), GitHub, VS Code Markdown preview, or
draw.io (Arrange → Insert → Advanced → Mermaid) to render the drawing.

---

## 1. Layered system architecture

The system uses a **3-layer architecture**: a presentation layer (console
menu), a business-logic layer (the parking engine and its domain classes),
and an in-memory data layer (the chosen data structures). All user input
crosses a validation boundary before it can reach the engine.

```mermaid
---
id: 53103569-9d16-45c9-806a-76eb572cdbbc
---
flowchart TB
    subgraph UI["PRESENTATION LAYER"]
        MENU["Menu-driven console<br/>main loop, options 0-12<br/>prints menus, receipts, reports"]
    end

    subgraph VAL["VALIDATION BOUNDARY - namespace input"]
        IV["I/O validation<br/>intInRange, code, vehicleType<br/>re-prompts, never crashes, EOF-safe"]
        DV["Date/Time validation<br/>format, ranges, days-per-month,<br/>leap years"]
    end

    subgraph BL["BUSINESS-LOGIC LAYER"]
        PS["ParkingSystem<br/>facade / engine"]
        TM["TariffManager<br/>live rates + controlled updates"]
        TL["Timeline guard<br/>global event clock"]
        VH["Vehicle hierarchy<br/>abstract base + 3 subclasses"]
        DT["DateTime<br/>exact minute arithmetic"]
    end

    subgraph DL["IN-MEMORY DATA LAYER - no database"]
        DS1[("slots_<br/>unordered_map - O(1)")]
        DS2[("freeSlots_<br/>map of sets - O(log n)")]
        DS3[("activeVehicles_<br/>unordered_map - O(1)")]
        DS4[("history_<br/>vector - append-only")]
        DS5[("historyByPlate_<br/>unordered_map index")]
        DS6[("dailyRevenue_<br/>map - date-sorted")]
    end

    MENU --> IV
    MENU --> DV
    IV --> PS
    DV --> PS
    PS --> TM
    PS --> TL
    PS --> VH
    PS --> DT
    PS --> DS1
    PS --> DS2
    PS --> DS3
    PS --> DS4
    PS --> DS5
    PS --> DS6
```

---

## 2. Core system components

```mermaid
flowchart LR
    T1["Slot Configuration<br/>Task 1<br/>add / remove / list slots"] --> E["ParkingSystem<br/>CORE ENGINE<br/>owns all data structures,<br/>enforces every business rule"]
    T2["Vehicle Entry<br/>Task 2<br/>unique plate, auto allocation,<br/>no double parking"] --> E
    T3["Duration and Fee<br/>Task 3<br/>ceiling-hour billing,<br/>live tariffs"] --> E
    T4["Vehicle Exit<br/>Task 4<br/>release slot, receipt,<br/>frozen rate record"] --> E
    E --> T5["Reports<br/>Task 5<br/>available slots, parked vehicles,<br/>history, daily revenue"]
```

| # | Component | Type | Responsibility |
|---|-----------|------|----------------|
| 1 | **Menu interface** (`main`, `printMenu`) | Presentation | Menu-driven console UI; routes the 13 options to the engine; displays results, receipts and reports. |
| 2 | **Input validators** (`namespace input`) | Validation | The only gateway for user input. Line-based reading; rejects empty/non-numeric/out-of-range/malformed values with a clear message and re-prompt; EOF-safe. |
| 3 | **`DateTime`** | Domain | Strict `YYYY-MM-DD HH:MM` parsing; calendar validation (ranges, days-per-month, leap years); exact minute arithmetic across days/months/years (civil day-count algorithm). |
| 4 | **`Vehicle` hierarchy** | Domain (OOP) | Abstract base `Vehicle`; `Motorcycle`, `Car`, `Truck` subclasses created through a factory and used polymorphically. |
| 5 | **`ParkingSlot`** | Domain | Encapsulates slot ID, supported vehicle type, zone, status, and the plate currently occupying it. |
| 6 | **`TariffManager`** | Domain | Holds the live hourly rates (defaults: Moto 500 / Car 1,000 / Truck 2,000 RWF); controlled, validated runtime updates; never touches completed records. |
| 7 | **`ParkingSystem`** | Engine (facade) | Single entry point for every operation. Owns all data structures, enforces business rules and the **global timeline clock** (no event may precede the last recorded event; no exit before entry). |
| 8 | **Transaction store** | Data | `Transaction` records freeze the rate at exit time, are appended to `history_`, indexed per plate, and aggregated into `dailyRevenue_`. |

---

## 3. UML class diagram (OOP design)

Abstraction, inheritance, polymorphism and encapsulation exactly as
implemented in `main.cpp`:

```mermaid
classDiagram
    direction TB

    class Vehicle {
        <<abstract>>
        -string plate_
        +plate() string
        +type()* VehicleType
        +typeName()* string
    }
    class Motorcycle {
        +type() VehicleType
        +typeName() string
    }
    class Car {
        +type() VehicleType
        +typeName() string
    }
    class Truck {
        +type() VehicleType
        +typeName() string
    }
    Vehicle <|-- Motorcycle
    Vehicle <|-- Car
    Vehicle <|-- Truck

    class DateTime {
        -int year_
        -int month_
        -int day_
        -int hour_
        -int minute_
        +isValid() bool
        +parse(text) bool
        +now() DateTime
        +totalMinutes() long
        +minutesBetween(from, to) long
        +toString() string
        +dateKey() string
    }

    class ParkingSlot {
        -string id_
        -VehicleType type_
        -string zone_
        -bool occupied_
        -string currentPlate_
        +occupy(plate) void
        +release() void
        +occupied() bool
    }

    class TariffManager {
        -map rates_
        +rate(type) long
        +update(type, newRate) bool
        +print() void
    }

    class ActiveTicket {
        +Vehicle vehicle
        +string slotId
        +DateTime entryTime
    }

    class Transaction {
        +string plate
        +string vehicleType
        +string slotId
        +DateTime entryTime
        +DateTime exitTime
        +long billedHours
        +long ratePerHour
        +long fee
    }

    class ParkingSystem {
        -unordered_map slots_
        -map freeSlots_
        -unordered_map activeVehicles_
        -vector history_
        -unordered_map historyByPlate_
        -map dailyRevenue_
        -TariffManager tariffs_
        -DateTime lastEventTime_
        +addSlot(id, type, zone) bool
        +removeSlot(id) bool
        +listSlots(onlyAvailable) void
        +registerEntry(plate, type, entry) bool
        +processExit(plate, exitTime) bool
        +listParkedVehicles() void
        +vehicleHistory(plate) void
        +allTransactions() void
        +dailyRevenueReport() void
    }

    ParkingSystem "1" *-- "many" ParkingSlot : owns
    ParkingSystem "1" *-- "many" ActiveTicket : tracks
    ParkingSystem "1" *-- "many" Transaction : records
    ParkingSystem "1" *-- "1" TariffManager : uses
    ActiveTicket "1" o-- "1" Vehicle : polymorphic
    ActiveTicket ..> DateTime
    Transaction ..> DateTime
```

---

## 4. Vehicle ENTRY flow (Task 2)

```mermaid
flowchart TD
    S(["Driver arrives"]) --> T["Read vehicle type<br/>1=Moto, 2=Car, 3=Truck"]
    T --> P["Show plate format reminder, read plate<br/>Car/Truck: RA+letter+3digits+letter e.g. RAD123B<br/>Moto: RB+3digits+letter e.g. RB001A"]
    P --> DT["Read entry time<br/>ENTER = system time<br/>or YYYY-MM-DD HH:MM"]
    DT --> V1{"Valid calendar<br/>date and time?"}
    V1 -- "No, e.g. Feb 30" --> DT
    V1 -- Yes --> V2{"Plate already<br/>parked?"}
    V2 -- Yes --> R1["REJECT:<br/>no double parking"] --> X(["Back to menu"])
    V2 -- No --> V3{"Time before last<br/>recorded event?"}
    V3 -- Yes --> R2["REJECT:<br/>timeline violation"] --> X
    V3 -- No --> V4{"Free slot for this<br/>vehicle type?"}
    V4 -- No --> R3["Graceful message:<br/>no slot available, try later"] --> X
    V4 -- Yes --> A["Allocate first free slot<br/>from sorted set, O(log n)"]
    A --> B["Mark slot Occupied,<br/>create ActiveTicket,<br/>advance global clock"]
    B --> OK(["Confirmation printed"])
```

---

## 5. Vehicle EXIT and fee calculation (Tasks 3 and 4)

```mermaid
sequenceDiagram
    actor Attendant
    participant UI as Console Menu
    participant V as Validators
    participant PS as ParkingSystem
    participant TM as TariffManager
    participant DS as Data Structures

    Attendant->>UI: Option 6 - Vehicle EXIT
    UI->>V: read plate + exit time
    V-->>UI: validated values (re-prompt on errors)
    UI->>PS: processExit(plate, exitTime)
    PS->>DS: find plate in activeVehicles_ O(1)
    alt vehicle not parked
        PS-->>UI: error "not currently parked"
    else exit before entry or timeline violation
        PS-->>UI: error showing both timestamps
    else valid exit
        PS->>PS: minutes = exact difference<br/>billedHours = ceil(minutes/60), min 1
        PS->>TM: rate(vehicleType) - current ACTIVE price
        TM-->>PS: e.g. 1000 RWF/hour
        PS->>PS: fee = billedHours x rate
        PS->>DS: release slot, re-insert into freeSlots_
        PS->>DS: append Transaction with FROZEN rate
        PS->>DS: update historyByPlate_ and dailyRevenue_
        PS->>PS: advance global event clock
        PS-->>UI: printed RECEIPT - duration, rate, total fee
    end
```

---

## 6. Parking slot life cycle

```mermaid
stateDiagram-v2
    [*] --> Available : addSlot()
    Available --> Occupied : registerEntry()<br/>vehicle allocated
    Occupied --> Available : processExit()<br/>slot released
    Available --> [*] : removeSlot()
    note right of Occupied
        removeSlot() is REFUSED
        while occupied
    end note
```

---

## 7. Price update rule (Task 3)

```mermaid
flowchart LR
    U["Attendant<br/>option 12"] --> C{"New rate valid?<br/>positive and at most 1,000,000"}
    C -- No --> E["Rejected with reason"]
    C -- Yes --> R["rates_ map updated<br/>live tariff only"]
    R --> N["Next exits billed<br/>at NEW rate"]
    R -. does NOT modify .-> H[("history_<br/>each record keeps its own<br/>frozen ratePerHour")]
```

---

## 8. Component-to-data-structure mapping (DSA justification)

| Operation | Data structure | Category | Complexity |
|---|---|---|---|
| Slot lookup by unique ID | `unordered_map<string, ParkingSlot>` | linear (hash table) | O(1) avg |
| Allocate / release free slot per type | `map<VehicleType, set<string>>` | non-linear (red-black trees) | O(log n) |
| "Is this plate already parked?" | `unordered_map<string, ActiveTicket>` | linear (hash table) | O(1) avg |
| Completed transaction log | `vector<Transaction>` | linear (dynamic array) | O(1) append, O(n) traversal |
| History of one plate | `unordered_map<string, vector<size_t>>` | hash table + index list | O(1) avg |
| Daily revenue report, date-sorted | `map<string, long long>` | non-linear (red-black tree) | O(log n) insert, in-order traversal |
| Live tariffs | `map<VehicleType, long long>` | non-linear | O(log n) |

## Why this architecture

- **Separation of concerns**: the UI never touches data structures directly,
  and the engine never reads from `cin` — every operation is a method that
  returns success/failure plus a message, which also makes it unit-testable.
- **Single facade** (`ParkingSystem`) keeps all invariants in one place:
  a slot can never be in `freeSlots_` while occupied, and the timeline clock
  advances only after an operation fully succeeds.
- **Scalability**: hash maps give O(1) lookups on the hot paths (entry/exit
  by plate, slot by ID); ordered structures are used only where sorted order
  is actually needed (slot allocation, daily report).
