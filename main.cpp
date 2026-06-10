/***********************************************************************************
 *  KIGALI CITY - SMART PARKING MANAGEMENT SYSTEM
 *  ---------------------------------------------------------------------------
 *  Data Structures & Algorithms practical project (C++ / in-memory only)
 *
 *  PROBLEM BEING SOLVED
 *  ---------------------------------------------------------------------------
 *  Kigali's manual paper-ticket parking causes queues, bad slot allocation,
 *  fee disputes and revenue leakage. This program replaces it with a
 *  menu-driven system that manages slots, vehicle entry/exit, accurate
 *  time-based billing and reliable reports - entirely with in-memory data
 *  structures (no database).
 *
 *  DATA STRUCTURE CHOICES (justification)
 *  ---------------------------------------------------------------------------
 *  1. unordered_map<string, ParkingSlot>  slots_
 *     -> Hash table: O(1) average lookup/insert/delete of a slot by its
 *        unique Slot ID. Every operation in the system references slots by
 *        ID, so the fastest possible by-ID access is what we need.
 *
 *  2. map<VehicleType, set<string>>       freeSlots_
 *     -> Balanced BST (non-linear): keeps the FREE slot IDs of every vehicle
 *        type sorted, so allocating the "first" suitable slot is O(log n)
 *        and releasing a slot (re-insert) is O(log n). Without this index we
 *        would scan ALL slots on every entry (O(n)).
 *
 *  3. unordered_map<string, ActiveTicket> activeVehicles_
 *     -> Hash table keyed by plate number: O(1) check that a vehicle is not
 *        parked twice, O(1) retrieval of its ticket at exit time.
 *
 *  4. vector<Transaction>                 history_
 *     -> Dynamic array (linear): completed transactions are append-only and
 *        traversed sequentially for reports. O(1) amortised insertion.
 *
 *  5. unordered_map<string, vector<size_t>> historyByPlate_
 *     -> Secondary index: O(1) average access to the full history of one
 *        plate (stores positions inside history_, not copies).
 *
 *  6. map<string, long long>              dailyRevenue_
 *     -> Ordered map keyed by date "YYYY-MM-DD": the daily revenue report
 *        comes out chronologically sorted by a simple in-order traversal.
 *
 *  OOP DESIGN
 *  ---------------------------------------------------------------------------
 *     - Abstraction : abstract class Vehicle with pure virtual methods.
 *     - Inheritance : Motorcycle, Car, Truck derive from Vehicle.
 *     - Polymorphism: vehicles are created through a factory function and
 *                     used through base-class pointers / virtual calls.
 *     - Encapsulation: every class keeps its data private behind methods.
 *
 *  VALIDATION & ROBUSTNESS
 *  ---------------------------------------------------------------------------
 *     - ALL console input is line-based and re-prompted on bad input:
 *       letters where numbers are expected, empty lines, symbols, huge
 *       numbers, injection-style strings... nothing can crash the program.
 *     - Plate numbers follow the official Rwandan formats and the expected
 *       format is always shown to the user BEFORE typing:
 *           Car/Truck  : RA + 1 letter + 3 digits + 1 letter   e.g. RAD123B
 *           Motorcycle : R  + 1 letter + 3 digits + 1 letter   e.g. RB001A
 *     - Dates are fully validated: strict YYYY-MM-DD HH:MM format, year
 *       range 2000-2100 (so "1/1/1" or year 0001 are impossible), month
 *       1-12, day checked against the real length of that month including
 *       LEAP YEARS, hour 0-23, minute 0-59.
 *     - TIMELINE LOGIC: the system keeps a global event clock. Every new
 *       event (entry or exit) must NOT happen before the previous recorded
 *       event, and a vehicle can never exit before it entered.
 ***********************************************************************************/

#include <iostream>      // console input / output (cin, cout)
#include <iomanip>       // output formatting (setw, setfill)
#include <sstream>       // string streams used for parsing and building text
#include <string>
#include <vector>        // linear structure: transaction history
#include <map>           // non-linear (red-black tree): sorted keys
#include <set>           // non-linear (red-black tree): sorted free slot IDs
#include <unordered_map> // hash tables: O(1) lookups by slot ID / plate
#include <memory>        // unique_ptr for polymorphic Vehicle ownership
#include <algorithm>     // all_of, transform, remove, find_if
#include <cctype>        // isdigit, isalpha, isalnum, toupper
#include <ctime>         // reading the current system clock

using namespace std;

/*==================================================================================
 *  1. VEHICLE TYPE
 *  A small, type-safe enum. Using "enum class" (instead of plain int codes)
 *  means the compiler refuses to mix a vehicle type with any other number,
 *  which prevents a whole family of bugs. The numeric values 1..3 match the
 *  menu choices the user types.
 *=================================================================================*/
enum class VehicleType { MOTORCYCLE = 1, CAR = 2, TRUCK = 3 };

/* Convert the enum into a human-readable word for menus, receipts, reports. */
static string typeToString(VehicleType t) {
    switch (t) {
        case VehicleType::MOTORCYCLE: return "Motorcycle";
        case VehicleType::CAR:        return "Car";
        case VehicleType::TRUCK:      return "Truck";
    }
    return "Unknown";   // unreachable, but keeps the compiler happy
}

/*==================================================================================
 *  2. DATE & TIME
 *  A self-contained calendar class. It does three jobs:
 *    (a) VALIDATE that a typed date really exists (leap years included),
 *    (b) COMPARE timestamps so the timeline rules can be enforced,
 *    (c) MEASURE the exact number of minutes between two timestamps - even
 *        across midnight, month ends and year ends - for correct billing.
 *  Everything is private; the only way to obtain a DateTime is through the
 *  validating constructors/parsers (encapsulation guarantees correctness).
 *=================================================================================*/
class DateTime {
private:
    int year_, month_, day_, hour_, minute_;

    /* Leap-year rule of the Gregorian calendar:
       divisible by 4 AND (not by 100 unless also by 400).
       Examples: 2024 leap, 2026 NOT leap, 1900 NOT leap, 2000 leap. */
    static bool isLeap(int y) {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }

    /* How many days does month m of year y really have?
       February asks isLeap(); everything else is a fixed table. */
    static int daysInMonth(int y, int m) {
        static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (m == 2 && isLeap(y)) return 29;
        return d[m - 1];
    }

    /* Howard Hinnant's "days from civil" algorithm: converts a calendar date
       into an exact count of days since 1970-01-01. This is the trick that
       makes duration arithmetic EXACT across months and years - we never
       approximate a month as 30 days. It works era by era (400-year blocks,
       146097 days each), which is why leap years are handled perfectly. */
    static long long daysFromCivil(int y, int m, int d) {
        y -= m <= 2;                                   // treat Jan/Feb as months 13/14 of prev year
        long long era = (y >= 0 ? y : y - 399) / 400;  // which 400-year era
        unsigned yoe  = static_cast<unsigned>(y - era * 400);             // year of era   [0,399]
        unsigned doy  = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // day of year   [0,365]
        unsigned doe  = yoe * 365 + yoe / 4 - yoe / 100 + doy;            // day of era    [0,146096]
        return era * 146097LL + doe - 719468LL;        // shift so 1970-01-01 == day 0
    }

public:
    /* Default: a harmless valid date (never shown to users). */
    DateTime() : year_(2000), month_(1), day_(1), hour_(0), minute_(0) {}
    DateTime(int y, int mo, int d, int h, int mi)
        : year_(y), month_(mo), day_(d), hour_(h), minute_(mi) {}

    /* THE calendar validator. Every rule is checked in order and the FIRST
       broken rule is reported in plain language so the user knows exactly
       what to fix. The year window 2000-2100 makes absurd inputs such as
       "0001-01-01" or "9999-..." impossible by design. */
    static bool isValid(int y, int mo, int d, int h, int mi, string& err) {
        if (y  < 2000 || y  > 2100) { err = "Year must be between 2000 and 2100.";   return false; }
        if (mo < 1    || mo > 12)   { err = "Month must be between 1 and 12.";       return false; }
        if (d  < 1 || d > daysInMonth(y, mo)) {        // real length of THAT month
            err = "Day must be between 1 and " + to_string(daysInMonth(y, mo)) +
                  " for " + to_string(y) + "-" + (mo < 10 ? "0" : "") + to_string(mo) + ".";
            return false;
        }
        if (h  < 0 || h  > 23) { err = "Hour must be between 0 and 23.";   return false; }
        if (mi < 0 || mi > 59) { err = "Minute must be between 0 and 59."; return false; }
        return true;
    }

    /* Strict parser for "YYYY-MM-DD HH:MM".
       Step 1: extract the five numbers AND the three separator characters;
               if anything is missing or a separator is wrong (e.g. the user
               typed "1/1/1" with slashes) -> format error.
       Step 2: refuse trailing garbage ("2026-06-10 08:00 hello").
       Step 3: run the full calendar validation above.
       Only if ALL three steps pass do we construct the DateTime. */
    static bool parse(const string& text, DateTime& out, string& err) {
        int y, mo, d, h, mi; char dash1, dash2, colon;
        istringstream iss(text);
        if (!(iss >> y >> dash1 >> mo >> dash2 >> d >> h >> colon >> mi) ||
            dash1 != '-' || dash2 != '-' || colon != ':') {
            err = "Format must be YYYY-MM-DD HH:MM (e.g. 2026-06-10 08:30).";
            return false;
        }
        string rest;
        if (iss >> rest) { err = "Unexpected extra characters after the time."; return false; }
        if (!isValid(y, mo, d, h, mi, err)) return false;
        out = DateTime(y, mo, d, h, mi);
        return true;
    }

    /* Read the machine's current local time (used when the attendant just
       presses ENTER instead of typing a time manually). */
    static DateTime now() {
        time_t t = time(nullptr);
        tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);   // thread-safe variant on Windows
#else
        localtime_r(&t, &local);   // thread-safe variant on Linux/Mac
#endif
        return DateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                        local.tm_hour, local.tm_min);
    }

    /* Convert this timestamp into ONE number: total minutes since 1970.
       Once both timestamps are single numbers, comparing them and
       subtracting them is trivial and cannot be wrong. */
    long long totalMinutes() const {
        return daysFromCivil(year_, month_, day_) * 24LL * 60LL + hour_ * 60LL + minute_;
    }
    bool operator< (const DateTime& o) const { return totalMinutes() <  o.totalMinutes(); }
    bool operator<=(const DateTime& o) const { return totalMinutes() <= o.totalMinutes(); }

    /* Exact parking duration in minutes. The caller guarantees from <= to
       (the exit-validation rules enforce that before we ever get here). */
    static long long minutesBetween(const DateTime& from, const DateTime& to) {
        return to.totalMinutes() - from.totalMinutes();
    }

    /* Pretty zero-padded text, e.g. "2026-06-10 08:05" (for receipts). */
    string toString() const {
        ostringstream oss;
        oss << setfill('0') << setw(4) << year_  << '-' << setw(2) << month_ << '-'
            << setw(2) << day_ << ' ' << setw(2) << hour_ << ':' << setw(2) << minute_;
        return oss.str();
    }

    /* Just the date part, e.g. "2026-06-10". Used as the KEY of the daily
       revenue map - identical exit dates accumulate into the same bucket. */
    string dateKey() const {
        ostringstream oss;
        oss << setfill('0') << setw(4) << year_ << '-' << setw(2) << month_
            << '-' << setw(2) << day_;
        return oss.str();
    }
};

/*==================================================================================
 *  3. VEHICLE HIERARCHY (abstraction + inheritance + polymorphism)
 *  Vehicle is ABSTRACT: it cannot be instantiated because type() and
 *  typeName() are pure virtual ("= 0"). Each concrete subclass answers
 *  those questions for itself. The rest of the program only ever talks to
 *  "a Vehicle" - it neither knows nor cares which subclass it is holding;
 *  the virtual call finds the right answer at runtime (polymorphism).
 *=================================================================================*/
class Vehicle {                                    // abstract base class
private:
    string plate_;                                 // encapsulated: read-only outside
protected:
    explicit Vehicle(string plate) : plate_(move(plate)) {}
public:
    virtual ~Vehicle() = default;                  // virtual dtor: safe delete via base ptr
    virtual VehicleType type()     const = 0;      // pure virtual -> abstraction
    virtual string      typeName() const = 0;      // pure virtual -> abstraction
    const string& plate() const { return plate_; } // controlled access (no setter!)
};

/* Each subclass overrides the two pure virtual methods - nothing more is
   needed; all shared behaviour lives in the base class. */
class Motorcycle : public Vehicle {
public:
    explicit Motorcycle(string p) : Vehicle(move(p)) {}
    VehicleType type()     const override { return VehicleType::MOTORCYCLE; }
    string      typeName() const override { return "Motorcycle"; }
};
class Car : public Vehicle {
public:
    explicit Car(string p) : Vehicle(move(p)) {}
    VehicleType type()     const override { return VehicleType::CAR; }
    string      typeName() const override { return "Car"; }
};
class Truck : public Vehicle {
public:
    explicit Truck(string p) : Vehicle(move(p)) {}
    VehicleType type()     const override { return VehicleType::TRUCK; }
    string      typeName() const override { return "Truck"; }
};

/* FACTORY: the one place that maps an enum value to a concrete subclass.
   Returns a unique_ptr so ownership (and automatic cleanup) is explicit. */
static unique_ptr<Vehicle> makeVehicle(VehicleType t, const string& plate) {
    switch (t) {
        case VehicleType::MOTORCYCLE: return make_unique<Motorcycle>(plate);
        case VehicleType::CAR:        return make_unique<Car>(plate);
        case VehicleType::TRUCK:      return make_unique<Truck>(plate);
    }
    return nullptr;   // unreachable - the enum has exactly three values
}

/*==================================================================================
 *  4. PARKING SLOT  (Task 1 entity)
 *  Holds the four required attributes: unique ID, supported vehicle type,
 *  zone, and status. Status is PRIVATE and can only change through
 *  occupy()/release(), so no other code can ever set an inconsistent state
 *  such as "occupied but with no vehicle" (encapsulation in action).
 *=================================================================================*/
class ParkingSlot {
private:
    string      id_;             // unique identifier, e.g. "C-01"
    VehicleType type_;           // which vehicle category may park here
    string      zone_;           // physical location, e.g. "ZONE-B"
    bool        occupied_;       // Available (false) / Occupied (true)
    string      currentPlate_;   // who is parked here; empty when free
public:
    ParkingSlot() : type_(VehicleType::CAR), occupied_(false) {}
    ParkingSlot(string id, VehicleType t, string zone)
        : id_(move(id)), type_(t), zone_(move(zone)), occupied_(false) {}

    /* Read-only accessors - outside code can look but not touch. */
    const string& id()    const { return id_; }
    VehicleType   type()  const { return type_; }
    const string& zone()  const { return zone_; }
    bool occupied()       const { return occupied_; }
    const string& plate() const { return currentPlate_; }

    /* The only two state transitions a slot can ever make. Both fields are
       updated together so the pair can never disagree. */
    void occupy(const string& plate) { occupied_ = true;  currentPlate_ = plate; }
    void release()                   { occupied_ = false; currentPlate_.clear(); }
};

/*==================================================================================
 *  5. TARIFF MANAGER  (Task 3 - controlled price updates)
 *  Owns the LIVE hourly prices. Key design point: completed transactions
 *  store their OWN copy of the rate at exit time, so changing a price here
 *  can never alter history - there is simply no pointer from history back
 *  to this table. That makes Rule 4 ("price updates must not affect
 *  completed records") true by construction, not by discipline.
 *=================================================================================*/
class TariffManager {
private:
    map<VehicleType, long long> rates_;            // RWF per started hour
public:
    /* Rule 1: the system STARTS with the default tariffs. */
    TariffManager() {
        rates_[VehicleType::MOTORCYCLE] = 500;     // per the assignment
        rates_[VehicleType::CAR]        = 1000;    // per the assignment
        rates_[VehicleType::TRUCK]      = 2000;    // our documented default
    }

    /* Current active price for one vehicle type (used at EXIT time only). */
    long long rate(VehicleType t) const { return rates_.at(t); }

    /* Rule 2: a CONTROLLED runtime update - the new price must be a
       positive amount within a sane upper bound, otherwise it is rejected
       with a reason and nothing changes. */
    bool update(VehicleType t, long long newRate, string& err) {
        if (newRate <= 0)      { err = "Price must be a positive amount.";          return false; }
        if (newRate > 1000000) { err = "Price above 1,000,000 RWF/h is rejected.";  return false; }
        rates_[t] = newRate;   // only the live tariff changes; history is untouched
        return true;
    }

    /* Show the tariff table (menu option 11 and at program start). */
    void print() const {
        cout << "\n  Current hourly tariffs:\n";
        for (const auto& [t, r] : rates_)
            cout << "    " << left << setw(12) << typeToString(t) << " : "
                 << r << " RWF/hour\n";
    }
};

/*==================================================================================
 *  6. RECORDS
 *=================================================================================*/

/* A vehicle that is INSIDE the parking right now. Note there is no fee
   field here - fees exist only at exit (assignment rule). The Vehicle is
   held polymorphically through the abstract base class. */
struct ActiveTicket {
    unique_ptr<Vehicle> vehicle;   // polymorphic: Motorcycle / Car / Truck
    string   slotId;               // where it was placed
    DateTime entryTime;            // when it came in
};

/* A COMPLETED parking visit - the permanent record (Task 4).
   ratePerHour and fee are FROZEN copies made at exit time; this is what
   guarantees later price updates can never rewrite history. */
struct Transaction {
    string      plate;
    string      vehicleType;       // text, produced by the virtual typeName()
    string      slotId;
    string      zone;
    DateTime    entryTime;
    DateTime    exitTime;
    long long   billedHours;       // duration rounded UP to full hours
    long long   ratePerHour;       // tariff that was active at exit (frozen)
    long long   fee;               // billedHours * ratePerHour
};

/*==================================================================================
 *  7. INPUT VALIDATION HELPERS
 *  EVERY piece of user input passes through this namespace - it is the
 *  single gateway between the keyboard and the engine. The shared strategy:
 *    - read a WHOLE LINE first (never "cin >> x", which leaves garbage in
 *      the stream and is the classic cause of infinite loops / crashes),
 *    - check it against the expected shape,
 *    - on failure print WHAT was wrong and re-prompt,
 *    - on end-of-file exit cleanly instead of spinning forever.
 *  Because of this design, letters where numbers are expected, symbols,
 *  empty lines, absurdly long numbers, or injection-style strings can never
 *  crash or corrupt the program - they just produce a polite message.
 *=================================================================================*/
namespace input {

    /* Read one raw line and trim whitespace from both ends.
       Returns false only at end-of-file (e.g. input file exhausted) so the
       caller can terminate gracefully instead of looping forever. */
    bool rawLine(string& line) {
        if (!getline(cin, line)) return false;
        auto notSpace = [](unsigned char c) { return !isspace(c); };
        line.erase(line.begin(), find_if(line.begin(), line.end(), notSpace));   // left trim
        line.erase(find_if(line.rbegin(), line.rend(), notSpace).base(), line.end()); // right trim
        return true;
    }

    /* Read an integer that must lie inside [lo, hi]. The checks, in order:
         1. not empty,
         2. every character is a digit (one leading '-' allowed),
         3. stoll() succeeds - wrapped in try/catch so a 30-digit "attack"
            number throws out_of_range and is caught instead of crashing,
         4. the value is inside the allowed range.
       The function only RETURNS when the user finally types a valid value -
       so callers never need to re-check. */
    long long intInRange(const string& prompt, long long lo, long long hi) {
        string line;
        while (true) {
            cout << prompt;
            if (!rawLine(line)) { cout << "\n[EOF] Exiting.\n"; exit(0); }
            if (line.empty()) { cout << "  [!] Input cannot be empty.\n"; continue; }
            bool numeric = !line.empty() &&
                all_of(line.begin() + (line[0] == '-' ? 1 : 0), line.end(),
                       [](unsigned char c) { return isdigit(c); });
            if (!numeric || line == "-") {
                cout << "  [!] Please enter a whole number.\n"; continue;
            }
            try {
                long long v = stoll(line);
                if (v < lo || v > hi) {
                    cout << "  [!] Value must be between " << lo << " and " << hi << ".\n";
                    continue;
                }
                return v;                            // finally valid -> give it back
            } catch (...) { cout << "  [!] Number is out of range.\n"; }
        }
    }

    /* Generic identifier reader used for SLOT IDs and ZONES:
       non-empty, length-bounded, only letters/digits/dash, auto-uppercased
       (so "c-01" and "C-01" can never become two different slots). */
    string code(const string& prompt, size_t minLen, size_t maxLen, const string& what) {
        string line;
        while (true) {
            cout << prompt;
            if (!rawLine(line)) { cout << "\n[EOF] Exiting.\n"; exit(0); }
            if (line.empty()) { cout << "  [!] " << what << " cannot be empty.\n"; continue; }
            if (line.size() < minLen || line.size() > maxLen) {
                cout << "  [!] " << what << " must be " << minLen << "-" << maxLen
                     << " characters long.\n";
                continue;
            }
            bool ok = all_of(line.begin(), line.end(), [](unsigned char c) {
                return isalnum(c) || c == '-';
            });
            if (!ok) {
                cout << "  [!] " << what << " may only contain letters, digits and '-'.\n";
                continue;
            }
            transform(line.begin(), line.end(), line.begin(),
                      [](unsigned char c) { return toupper(c); });
            return line;
        }
    }

    /* Menu-style choice of vehicle type, restricted to 1..3 by intInRange. */
    VehicleType vehicleType() {
        long long c = intInRange("  Vehicle type (1=Motorcycle, 2=Car, 3=Truck): ", 1, 3);
        return static_cast<VehicleType>(c);
    }

    /*------------------------------------------------------------------------------
     * RWANDAN PLATE NUMBER FORMATS
     *   Car / Truck : RA + 1 letter + 3 digits + 1 letter      e.g. RAD123B
     *                 pattern  R A _ - - - _   (_ = letter, - = digit)
     *   Motorcycle  : R + 1 letter + 3 digits + 1 letter       e.g. RB001A
     *                 pattern  R _ - - - _     (_ = letter, - = digit)
     * Both validators check the plate character by character and report the
     * FIRST rule that is broken, so the user knows exactly what to fix.
     * Spaces are tolerated and removed ("RB 001 A" -> "RB001A") and
     * lowercase is uppercased before checking.
     *-----------------------------------------------------------------------------*/

    /* Car/Truck plate: exactly 7 chars  ->  R A letter digit digit digit letter */
    bool plateMatchesCarTruck(const string& p, string& err) {
        if (p.size() != 7) {
            err = "must be exactly 7 characters (RA + letter + 3 digits + letter).";
            return false;
        }
        if (p[0] != 'R' || p[1] != 'A') { err = "must start with 'RA'."; return false; }
        if (!isalpha((unsigned char)p[2])) {
            err = "3rd character must be a LETTER (e.g. RAD...).";
            return false;
        }
        for (int i = 3; i <= 5; ++i)                  // positions 4..6: the digits
            if (!isdigit((unsigned char)p[i])) {
                err = "characters 4-6 must be three DIGITS (e.g. RAD123...).";
                return false;
            }
        if (!isalpha((unsigned char)p[6])) {
            err = "last character must be a LETTER (e.g. RAD123B).";
            return false;
        }
        return true;
    }

    /* Motorcycle plate: exactly 6 chars ->  R letter digit digit digit letter
       (pattern R_---_ given in the requirements: _ = letter, - = digit).   */
    bool plateMatchesMoto(const string& p, string& err) {
        if (p.size() != 6) {
            err = "must be exactly 6 characters (R + letter + 3 digits + letter).";
            return false;
        }
        if (p[0] != 'R') { err = "must start with 'R'."; return false; }
        if (!isalpha((unsigned char)p[1])) {
            err = "2nd character must be a LETTER (e.g. RB...).";
            return false;
        }
        for (int i = 2; i <= 4; ++i)                  // positions 3..5: the digits
            if (!isdigit((unsigned char)p[i])) {
                err = "characters 3-5 must be three DIGITS (e.g. RB001...).";
                return false;
            }
        if (!isalpha((unsigned char)p[5])) {
            err = "last character must be a LETTER (e.g. RB001A).";
            return false;
        }
        return true;
    }

    /* Normalise raw plate input BEFORE format checking:
         - remove inner spaces (so "RB 001 A" is accepted),
         - refuse empty / overlong / non-alphanumeric input,
         - uppercase everything (rad123b -> RAD123B).
       This is also the anti-abuse barrier: names ("JOHN"), pure numbers,
       symbols, or injection strings all die here or in the format check. */
    bool normalisePlate(string& line, string& err) {
        line.erase(remove(line.begin(), line.end(), ' '), line.end());
        if (line.empty())     { err = "Plate number cannot be empty."; return false; }
        if (line.size() > 10) { err = "Plate number is too long.";     return false; }
        for (unsigned char c : line)
            if (!isalnum(c)) { err = "Plate may only contain letters and digits."; return false; }
        transform(line.begin(), line.end(), line.begin(),
                  [](unsigned char c) { return toupper(c); });
        return true;
    }

    /* Plate entry when the vehicle TYPE IS ALREADY KNOWN (registration).
       The expected format is printed as a REMINDER before every attempt -
       the user is never left guessing. Loops until the plate is valid. */
    string plateFor(VehicleType t) {
        const bool moto = (t == VehicleType::MOTORCYCLE);
        const string hint = moto
            ? "R + 1 letter + 3 digits + 1 letter   (example: RB001A or 'RB 001 A')"
            : "RA + 1 letter + 3 digits + 1 letter   (example: RAD123B)";
        while (true) {
            cout << "  >> Plate format for a " << typeToString(t) << " is: " << hint << "\n"
                 << "  Plate number: ";
            string line;
            if (!rawLine(line)) { cout << "\n[EOF] Exiting.\n"; exit(0); }
            string err;
            if (!normalisePlate(line, err)) { cout << "  [!] " << err << "\n"; continue; }
            bool ok = moto ? plateMatchesMoto(line, err)
                           : plateMatchesCarTruck(line, err);
            if (ok) return line;
            cout << "  [!] Invalid " << typeToString(t) << " plate '" << line
                 << "': " << err << "\n";
        }
    }

    /* Plate entry when the type is NOT known yet (exit / history lookup):
       we accept the plate if it matches EITHER legal format. The two
       formats cannot be confused with each other because their lengths
       differ (7 vs 6 characters). */
    string plateAny(const string& prompt) {
        while (true) {
            cout << prompt
                 << "\n  (Car/Truck: RAD123B style | Motorcycle: RB001A style): ";
            string line;
            if (!rawLine(line)) { cout << "\n[EOF] Exiting.\n"; exit(0); }
            string err;
            if (!normalisePlate(line, err)) { cout << "  [!] " << err << "\n"; continue; }
            string e1, e2;
            if (plateMatchesCarTruck(line, e1) || plateMatchesMoto(line, e2)) return line;
            cout << "  [!] '" << line << "' is not a valid plate. Expected either\n"
                 << "      RA + letter + 3 digits + letter (Car/Truck, e.g. RAD123B)\n"
                 << "      or R + letter + 3 digits + letter (Motorcycle, e.g. RB001A).\n";
        }
    }

    /* Date-time entry. The attendant has two options:
         - press ENTER          -> the current SYSTEM time is used,
         - type a timestamp     -> it goes through the strict parser and the
                                   full calendar validation; any defect is
                                   explained and the prompt repeats.
       So an impossible input like "1/1/1" (wrong separators) or
       "2026-02-30 10:00" (February has no day 30) can never get through. */
    DateTime dateTime(const string& label) {
        while (true) {
            cout << "  " << label << " - press ENTER for current system time,\n"
                 << "  or type it as YYYY-MM-DD HH:MM : ";
            string line;
            if (!rawLine(line)) { cout << "\n[EOF] Exiting.\n"; exit(0); }
            if (line.empty()) {
                DateTime n = DateTime::now();
                cout << "  -> Using current time: " << n.toString() << "\n";
                return n;
            }
            DateTime dt; string err;
            if (DateTime::parse(line, dt, err)) return dt;
            cout << "  [!] Invalid date/time: " << err << "\n";
        }
    }
} // namespace input

/*==================================================================================
 *  8. PARKING SYSTEM  -  the core engine
 *  This is the FACADE of the whole application: it owns every data
 *  structure and is the ONLY place where business rules are enforced.
 *  The console code never touches the data directly - it can only call
 *  these methods, each of which returns success/failure plus a message.
 *  That separation is what keeps every invariant safe:
 *    - a slot ID can never appear twice,
 *    - a slot can never be in freeSlots_ while occupied,
 *    - a plate can never be active twice,
 *    - the global clock only advances when an operation fully succeeds.
 *=================================================================================*/
class ParkingSystem {
private:
    /* --- the six data structures (justified in the file header) --- */
    unordered_map<string, ParkingSlot>      slots_;          // slotId -> slot     O(1)
    map<VehicleType, set<string>>           freeSlots_;      // type   -> free IDs O(log n)
    unordered_map<string, ActiveTicket>     activeVehicles_; // plate  -> ticket   O(1)
    vector<Transaction>                     history_;        // append-only log
    unordered_map<string, vector<size_t>>   historyByPlate_; // plate  -> indexes into history_
    map<string, long long>                  dailyRevenue_;   // "YYYY-MM-DD" -> RWF

    TariffManager                           tariffs_;        // live prices
    DateTime                                lastEventTime_;  // global timeline clock
    bool                                    anyEventYet_ = false;

    /* TIMELINE RULE: the parking lot is a real place - events happen one
       after another. If the attendant tries to record an event EARLIER
       than the last recorded one, something is wrong (typo or fraud), so
       we refuse and show both timestamps. */
    bool timelineOk(const DateTime& t, string& err) const {
        if (anyEventYet_ && t < lastEventTime_) {
            err = "Timeline violation: this time (" + t.toString() +
                  ") is before the last recorded event (" + lastEventTime_.toString() +
                  "). Events must be entered in chronological order.";
            return false;
        }
        return true;
    }

    /* Move the global clock forward. Called ONLY after an operation has
       fully succeeded - failed attempts must not advance time. */
    void advanceClock(const DateTime& t) { lastEventTime_ = t; anyEventYet_ = true; }

public:
    /*------------------------------ Task 1: slots -------------------------------
     * addSlot - INSERTION into two structures kept in sync:
     *   1. uniqueness: if the ID already exists we refuse (the map lookup
     *      is O(1), so this check is essentially free),
     *   2. the slot object goes into the master registry slots_,
     *   3. its ID also goes into freeSlots_[type] because a new slot is
     *      born Available.
     *----------------------------------------------------------------------------*/
    bool addSlot(const string& id, VehicleType t, const string& zone, string& err) {
        if (slots_.count(id)) {                        // uniqueness check first
            err = "Slot ID '" + id + "' already exists.";
            return false;
        }
        slots_.emplace(id, ParkingSlot(id, t, zone));  // insertion (hash table)
        freeSlots_[t].insert(id);                      // insertion (BST set)
        return true;
    }

    /* removeSlot - DELETION with two protective rules:
       (a) the slot must exist, (b) it must not be occupied (otherwise we
       would orphan a parked vehicle). Only then is it deleted from BOTH
       structures so they never disagree. */
    bool removeSlot(const string& id, string& err) {
        auto it = slots_.find(id);
        if (it == slots_.end())   { err = "Slot ID '" + id + "' does not exist.";          return false; }
        if (it->second.occupied()){ err = "Slot '" + id + "' is occupied by " +
                                          it->second.plate() + " and cannot be removed."; return false; }
        freeSlots_[it->second.type()].erase(id);       // delete from the free index
        slots_.erase(it);                              // delete from the registry
        return true;
    }

    /* listSlots - TRAVERSAL of the registry. With onlyAvailable=true the
       occupied ones are skipped (menu option 4); otherwise everything is
       shown with its live status and, if occupied, by whom. */
    void listSlots(bool onlyAvailable) const {
        cout << "\n  " << left << setw(10) << "Slot ID" << setw(14) << "Type"
             << setw(12) << "Zone" << setw(12) << "Status" << "Vehicle\n";
        cout << "  " << string(58, '-') << "\n";
        size_t shown = 0;
        for (const auto& [id, s] : slots_) {           // O(n) walk over all slots
            if (onlyAvailable && s.occupied()) continue;
            cout << "  " << left << setw(10) << s.id() << setw(14)
                 << typeToString(s.type()) << setw(12) << s.zone()
                 << setw(12) << (s.occupied() ? "Occupied" : "Available")
                 << (s.occupied() ? s.plate() : "-") << "\n";
            ++shown;
        }
        if (shown == 0)
            cout << "  (no " << (onlyAvailable ? "available " : "") << "slots)\n";
        cout << "  Total shown: " << shown << " of " << slots_.size() << " configured slot(s)\n";
    }

    /*--------------------------- Task 2: vehicle entry ---------------------------
     * registerEntry - the gate barrier. The checks run in a deliberate
     * order, CHEAPEST first, and ALL of them before anything is modified,
     * so a failed entry leaves the system in exactly the state it was in:
     *   1. O(1)      - is this plate already inside? (no double parking)
     *   2. O(1)      - does the entry time respect the global timeline?
     *   3. O(log n)  - is there a free slot of the right type?
     * Only when all three pass do we mutate: take the first free slot ID
     * (the set is sorted, so allocation is deterministic), mark the slot
     * occupied, create the polymorphic ticket, and advance the clock.
     *----------------------------------------------------------------------------*/
    bool registerEntry(const string& plate, VehicleType t,
                       const DateTime& entry, string& msg) {
        if (activeVehicles_.count(plate)) {            // rule: no double parking
            msg = "Vehicle " + plate + " is already parked in slot " +
                  activeVehicles_[plate].slotId + " (entered " +
                  activeVehicles_[plate].entryTime.toString() + ").";
            return false;
        }
        if (!timelineOk(entry, msg)) return false;     // rule: chronological order

        auto fit = freeSlots_.find(t);
        if (fit == freeSlots_.end() || fit->second.empty()) {   // graceful handling
            msg = "Sorry, no available " + typeToString(t) +
                  " slot right now. The vehicle cannot be admitted - please try later.";
            return false;
        }

        /* --- all checks passed: NOW we may modify state --- */
        string slotId = *fit->second.begin();          // first (lowest) free ID, O(log n)
        fit->second.erase(fit->second.begin());        // remove it from the free pool
        slots_[slotId].occupy(plate);                  // flip the slot to Occupied

        ActiveTicket ticket;
        ticket.vehicle   = makeVehicle(t, plate);      // polymorphic creation (factory)
        ticket.slotId    = slotId;
        ticket.entryTime = entry;
        activeVehicles_[plate] = move(ticket);         // O(1) insertion by plate
        advanceClock(entry);                           // timeline moves forward

        msg = "Vehicle " + plate + " (" + typeToString(t) + ") parked in slot " +
              slotId + " [zone " + slots_[slotId].zone() + "] at " + entry.toString() + ".";
        return true;
    }

    /*--------------------- Tasks 3 & 4: exit, fee, records ----------------------
     * processExit - the heart of the billing logic. Steps:
     *   1. find the ticket by plate (O(1)); refuse if not parked,
     *   2. refuse if exit < entry (per-vehicle timeline) or if exit is
     *      before the last recorded event (global timeline),
     *   3. FEE: exact minutes -> billed hours = ceiling(minutes/60), with a
     *      minimum of 1 hour ("partial hours charged as full hours":
     *      15 min -> 1 h, 1 h 20 -> 2 h). The (minutes + 59) / 60 integer
     *      trick computes the ceiling without floating point,
     *   4. the rate is read from TariffManager NOW - i.e. the price that is
     *      ACTIVE at exit time (Rule 3),
     *   5. release the slot back into the free pool,
     *   6. store the Transaction with FROZEN rate+fee copies (Rule 4),
     *      index it by plate, add the fee to that day's revenue bucket,
     *   7. delete the active ticket and advance the global clock,
     *   8. build the receipt text that the menu will display.
     * As in registerEntry, every validation happens BEFORE any mutation.
     *----------------------------------------------------------------------------*/
    bool processExit(const string& plate, const DateTime& exitTime, string& msg) {
        auto it = activeVehicles_.find(plate);         // step 1: O(1) lookup
        if (it == activeVehicles_.end()) {
            msg = "Vehicle " + plate + " is not currently parked.";
            return false;
        }
        ActiveTicket& tk = it->second;
        if (exitTime < tk.entryTime) {                 // step 2a: per-vehicle rule
            msg = "Exit time (" + exitTime.toString() + ") cannot be before entry time (" +
                  tk.entryTime.toString() + ").";
            return false;
        }
        if (!timelineOk(exitTime, msg)) return false;  // step 2b: global rule

        /* step 3+4: duration and fee */
        long long minutes     = DateTime::minutesBetween(tk.entryTime, exitTime);
        long long billedHours = (minutes + 59) / 60;   // ceiling division
        if (billedHours == 0) billedHours = 1;         // minimum charge: 1 hour
        long long rate = tariffs_.rate(tk.vehicle->type());   // price active NOW
        long long fee  = billedHours * rate;

        /* step 5: give the slot back */
        ParkingSlot& slot = slots_[tk.slotId];
        slot.release();                                // status -> Available
        freeSlots_[slot.type()].insert(slot.id());     // back into the free pool

        /* step 6: permanent record with frozen pricing */
        Transaction tr;
        tr.plate       = plate;
        tr.vehicleType = tk.vehicle->typeName();       // polymorphic virtual call
        tr.slotId      = tk.slotId;
        tr.zone        = slot.zone();
        tr.entryTime   = tk.entryTime;
        tr.exitTime    = exitTime;
        tr.billedHours = billedHours;
        tr.ratePerHour = rate;                         // frozen copy
        tr.fee         = fee;                          // frozen copy
        history_.push_back(tr);                        // append to the log
        historyByPlate_[plate].push_back(history_.size() - 1);  // index by plate
        dailyRevenue_[exitTime.dateKey()] += fee;      // aggregate by exit date

        /* step 7: the vehicle is no longer inside */
        activeVehicles_.erase(it);
        advanceClock(exitTime);

        /* step 8: receipt - shows the REAL duration AND the billed hours so
           there can be no dispute about how the fee was computed. */
        ostringstream oss;
        oss << "\n  ===== PARKING RECEIPT =====\n"
            << "  Plate        : " << tr.plate << "\n"
            << "  Vehicle type : " << tr.vehicleType << "\n"
            << "  Slot / Zone  : " << tr.slotId << " / " << tr.zone << "\n"
            << "  Entry time   : " << tr.entryTime.toString() << "\n"
            << "  Exit time    : " << tr.exitTime.toString() << "\n"
            << "  Duration     : " << minutes / 60 << "h " << minutes % 60
            << "min  -> billed as " << billedHours << " hour(s)\n"
            << "  Rate         : " << rate << " RWF/hour\n"
            << "  TOTAL FEE    : " << fee << " RWF\n"
            << "  ===========================";
        msg = oss.str();
        return true;
    }

    /*------------------------------ Task 5: reports -----------------------------*/

    /* Report: who is inside right now. A simple traversal of the active
       hash map; each row shows plate, type (via virtual call), slot, time. */
    void listParkedVehicles() const {
        cout << "\n  Currently parked vehicles: " << activeVehicles_.size() << "\n";
        if (activeVehicles_.empty()) return;
        cout << "  " << left << setw(12) << "Plate" << setw(14) << "Type"
             << setw(10) << "Slot" << "Entry time\n";
        cout << "  " << string(54, '-') << "\n";
        for (const auto& [plate, tk] : activeVehicles_)
            cout << "  " << left << setw(12) << plate << setw(14)
                 << tk.vehicle->typeName() << setw(10) << tk.slotId
                 << tk.entryTime.toString() << "\n";
    }

    /* Report: full history of ONE plate. Thanks to the historyByPlate_
       index this is O(1) to find + O(k) to print the k visits - we never
       scan the whole history. Also mentions a current stay if any. */
    void vehicleHistory(const string& plate) const {
        auto it = historyByPlate_.find(plate);
        cout << "\n  History for " << plate << ":\n";
        if (it == historyByPlate_.end() || it->second.empty()) {
            cout << "  (no completed parking records)\n";
        } else {
            for (size_t idx : it->second) {            // follow the stored indexes
                const Transaction& tr = history_[idx];
                cout << "  - " << tr.entryTime.toString() << " -> "
                     << tr.exitTime.toString() << " | slot " << tr.slotId
                     << " | " << tr.billedHours << " h x " << tr.ratePerHour
                     << " = " << tr.fee << " RWF\n";
            }
        }
        auto act = activeVehicles_.find(plate);        // is it inside right now?
        if (act != activeVehicles_.end())
            cout << "  * Currently parked in slot " << act->second.slotId
                 << " since " << act->second.entryTime.toString() << "\n";
    }

    /* Report: every completed transaction, in the order they happened
       (the vector preserves insertion order = chronological order). */
    void allTransactions() const {
        cout << "\n  All completed transactions: " << history_.size() << "\n";
        for (const Transaction& tr : history_)         // linear traversal
            cout << "  - " << left << setw(12) << tr.plate << tr.entryTime.toString()
                 << " -> " << tr.exitTime.toString() << " | slot " << tr.slotId
                 << " | " << tr.fee << " RWF\n";
        if (history_.empty()) cout << "  (none yet)\n";
    }

    /* Report: revenue per day. dailyRevenue_ is an ordered map keyed by
       "YYYY-MM-DD", so iterating it IS the sorted report - no extra sort
       step needed. A grand total is accumulated along the way. */
    void dailyRevenueReport() const {
        cout << "\n  Daily revenue report:\n";
        if (dailyRevenue_.empty()) { cout << "  (no revenue recorded yet)\n"; return; }
        long long total = 0;
        for (const auto& [day, amount] : dailyRevenue_) {   // in-order = by date
            cout << "  " << day << " : " << amount << " RWF\n";
            total += amount;
        }
        cout << "  " << string(28, '-') << "\n  TOTAL      : " << total << " RWF\n";
    }

    /*------------------------------ small helpers -------------------------------*/
    TariffManager&       tariffs()       { return tariffs_; }
    const TariffManager& tariffs() const { return tariffs_; }
    bool hasSlots() const { return !slots_.empty(); }

    /* Optional demo data so the marker can try the system in seconds
       without configuring slots first (offered once at startup). */
    void seedDemoSlots() {
        string e;   // error sink - these hard-coded IDs can never collide
        addSlot("M-01", VehicleType::MOTORCYCLE, "ZONE-A", e);
        addSlot("M-02", VehicleType::MOTORCYCLE, "ZONE-A", e);
        addSlot("C-01", VehicleType::CAR,        "ZONE-B", e);
        addSlot("C-02", VehicleType::CAR,        "ZONE-B", e);
        addSlot("C-03", VehicleType::CAR,        "ZONE-C", e);
        addSlot("T-01", VehicleType::TRUCK,      "ZONE-D", e);
    }
};

/*==================================================================================
 *  9. MENU-DRIVEN CONSOLE INTERFACE
 *  The presentation layer. It contains NO business logic at all - it only
 *  (a) shows the menu, (b) collects validated input through namespace
 *  input, (c) calls one engine method, (d) prints the returned message.
 *  This separation means the engine could be reused unchanged behind a GUI
 *  or a web API, and it can be unit-tested without a keyboard.
 *=================================================================================*/
static void printMenu() {
    cout << "\n================ KIGALI SMART PARKING SYSTEM ================\n"
         << "  1. Add a parking slot\n"
         << "  2. Remove a parking slot\n"
         << "  3. View ALL slots\n"
         << "  4. View AVAILABLE slots only\n"
         << "  5. Vehicle ENTRY\n"
         << "  6. Vehicle EXIT (fee calculation & receipt)\n"
         << "  7. View currently parked vehicles\n"
         << "  8. Vehicle history (by plate)\n"
         << "  9. All completed transactions\n"
         << " 10. Daily revenue report\n"
         << " 11. View current tariffs\n"
         << " 12. Update a tariff (controlled)\n"
         << "  0. Exit program\n"
         << "=============================================================\n";
}

int main() {
    ParkingSystem system;    // the engine - constructed with default tariffs

    cout << "*************************************************************\n"
         << "*      KIGALI CITY - SMART PARKING MANAGEMENT SYSTEM        *\n"
         << "*        DSA Practical Project (in-memory, C++)             *\n"
         << "*************************************************************\n";
    system.tariffs().print();          // Rule 1: show the default tariffs at start

    /* Offer demo slots once, so the system is usable immediately. */
    long long seed = input::intInRange(
        "\nLoad 6 sample parking slots for a quick demo? (1=Yes, 0=No): ", 0, 1);
    if (seed == 1) {
        system.seedDemoSlots();
        cout << "  -> Sample slots loaded (M-01, M-02, C-01, C-02, C-03, T-01).\n";
    }

    /* The main loop: every iteration shows the menu, reads ONE validated
       choice, executes it, and reports the outcome. The loop only ends
       when the user picks 0 (or the input stream ends). */
    while (true) {
        printMenu();
        long long choice = input::intInRange("Select an option (0-12): ", 0, 12);
        string msg;   // engine methods write their success/error text here

        switch (choice) {
        case 1: {   /* ---- add slot (Task 1 insertion) ---- */
            string id   = input::code("  New Slot ID (e.g. C-04): ", 1, 10, "Slot ID");
            VehicleType t = input::vehicleType();
            string zone = input::code("  Zone (e.g. ZONE-A): ", 1, 12, "Zone");
            if (system.addSlot(id, t, zone, msg))
                cout << "  [OK] Slot " << id << " added for " << typeToString(t)
                     << " in " << zone << ".\n";
            else cout << "  [X] " << msg << "\n";      // e.g. duplicate ID
            break;
        }
        case 2: {   /* ---- remove slot (Task 1 deletion, guarded) ---- */
            string id = input::code("  Slot ID to remove: ", 1, 10, "Slot ID");
            if (system.removeSlot(id, msg)) cout << "  [OK] Slot " << id << " removed.\n";
            else cout << "  [X] " << msg << "\n";      // missing or occupied
            break;
        }
        case 3: system.listSlots(false); break;        /* all slots          */
        case 4: system.listSlots(true);  break;        /* available only     */
        case 5: {   /* ---- vehicle entry (Task 2) ----
                       The vehicle TYPE is asked FIRST on purpose: the plate
                       format depends on the type, and we want to show the
                       correct format reminder before the user types. */
            if (!system.hasSlots()) {
                cout << "  [X] No slots configured yet - add slots first (option 1).\n";
                break;
            }
            VehicleType t = input::vehicleType();      // type first ...
            string plate  = input::plateFor(t);        // ... so the right format is shown
            DateTime entry = input::dateTime("Entry time");
            if (system.registerEntry(plate, t, entry, msg)) cout << "  [OK] " << msg << "\n";
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 6: {   /* ---- vehicle exit (Tasks 3 & 4) ---- */
            string plate = input::plateAny("  Plate number of exiting vehicle");
            DateTime exitT = input::dateTime("Exit time");
            if (system.processExit(plate, exitT, msg)) cout << msg << "\n";  // the receipt
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 7: system.listParkedVehicles(); break;
        case 8: {   /* ---- one vehicle's full history ---- */
            string plate = input::plateAny("  Plate number to look up");
            system.vehicleHistory(plate);
            break;
        }
        case 9:  system.allTransactions();    break;
        case 10: system.dailyRevenueReport(); break;
        case 11: system.tariffs().print();    break;
        case 12: {  /* ---- controlled price update (Task 3, Rules 2-4) ---- */
            system.tariffs().print();                  // show current prices first
            VehicleType t = input::vehicleType();
            long long newRate = input::intInRange(
                "  New hourly rate in RWF (1 - 1,000,000): ", 1, 1000000);
            if (system.tariffs().update(t, newRate, msg))
                cout << "  [OK] " << typeToString(t) << " tariff is now " << newRate
                     << " RWF/hour. Completed records keep their old prices.\n";
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 0:     /* ---- clean shutdown ---- */
            cout << "\nThank you for using the Kigali Smart Parking System. Goodbye!\n";
            return 0;
        }
    }
}
