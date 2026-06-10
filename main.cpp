/***********************************************************************************
 *  KIGALI CITY - SMART PARKING MANAGEMENT SYSTEM
 *  ---------------------------------------------------------------------------
 *  Data Structures & Algorithms practical project (C++ / in-memory only)
 *
 *  DATA STRUCTURE CHOICES (justification):
 *  ---------------------------------------------------------------------------
 *  1. unordered_map<string, ParkingSlot>  slots_
 *     -> Hash table: O(1) average lookup/insert/delete of a slot by its unique ID.
 *
 *  2. map<VehicleType, set<string>>       freeSlots_
 *     -> Balanced BST (non-linear): keeps the FREE slot IDs of every vehicle
 *        type sorted, so allocation of the "first" suitable slot is O(log n)
 *        and releasing a slot (re-insert) is O(log n).
 *
 *  3. unordered_map<string, ActiveTicket> activeVehicles_
 *     -> Hash table keyed by plate number: O(1) check that a vehicle is not
 *        parked twice, O(1) retrieval at exit time.
 *
 *  4. vector<Transaction>                 history_
 *     -> Dynamic array (linear): completed transactions are append-only and
 *        traversed sequentially for reports. O(1) amortised insertion.
 *
 *  5. unordered_map<string, vector<size_t>> historyByPlate_
 *     -> Secondary index: O(1) average access to the full history of one plate.
 *
 *  6. map<string, long long>              dailyRevenue_
 *     -> Ordered map keyed by date "YYYY-MM-DD": daily revenue report comes out
 *        chronologically sorted by simple in-order traversal.
 *
 *  OOP:
 *     - Abstraction / Inheritance / Polymorphism: abstract class Vehicle with
 *       concrete subclasses Motorcycle, Car, Truck (virtual methods).
 *     - Encapsulation: every class keeps its data private behind methods.
 *
 *  VALIDATION:
 *     - All numeric menu input is line-based and re-prompted on bad input
 *       (no crashes / infinite loops on letters, empty lines, etc.).
 *     - Plate numbers, slot IDs and zones are format-checked.
 *     - Dates are fully validated (ranges, days per month, leap years).
 *     - TIMELINE LOGIC: the system keeps a global event clock. Every new
 *       event (entry or exit) must NOT happen before the previous recorded
 *       event, and a vehicle can never exit before it entered.
 ***********************************************************************************/

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cctype>
#include <ctime>

using namespace std;

/*==================================================================================
 *  1. VEHICLE TYPE
 *=================================================================================*/
enum class VehicleType { MOTORCYCLE = 1, CAR = 2, TRUCK = 3 };

static string typeToString(VehicleType t) {
    switch (t) {
        case VehicleType::MOTORCYCLE: return "Motorcycle";
        case VehicleType::CAR:        return "Car";
        case VehicleType::TRUCK:      return "Truck";
    }
    return "Unknown";
}

/*==================================================================================
 *  2. DATE & TIME  (full calendar validation + exact minute arithmetic)
 *=================================================================================*/
class DateTime {
private:
    int year_, month_, day_, hour_, minute_;

    static bool isLeap(int y) {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }
    static int daysInMonth(int y, int m) {
        static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (m == 2 && isLeap(y)) return 29;
        return d[m - 1];
    }
    /* Howard Hinnant's "days from civil" algorithm: exact day count since
       1970-01-01, handles leap years correctly. */
    static long long daysFromCivil(int y, int m, int d) {
        y -= m <= 2;
        long long era = (y >= 0 ? y : y - 399) / 400;
        unsigned yoe  = static_cast<unsigned>(y - era * 400);                 // [0,399]
        unsigned doy  = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;      // [0,365]
        unsigned doe  = yoe * 365 + yoe / 4 - yoe / 100 + doy;                // [0,146096]
        return era * 146097LL + doe - 719468LL;
    }

public:
    DateTime() : year_(2000), month_(1), day_(1), hour_(0), minute_(0) {}
    DateTime(int y, int mo, int d, int h, int mi)
        : year_(y), month_(mo), day_(d), hour_(h), minute_(mi) {}

    /* Static validator: every rule of the calendar is checked here. */
    static bool isValid(int y, int mo, int d, int h, int mi, string& err) {
        if (y  < 2000 || y  > 2100) { err = "Year must be between 2000 and 2100.";   return false; }
        if (mo < 1    || mo > 12)   { err = "Month must be between 1 and 12.";       return false; }
        if (d  < 1 || d > daysInMonth(y, mo)) {
            err = "Day must be between 1 and " + to_string(daysInMonth(y, mo)) +
                  " for " + to_string(y) + "-" + (mo < 10 ? "0" : "") + to_string(mo) + ".";
            return false;
        }
        if (h  < 0 || h  > 23) { err = "Hour must be between 0 and 23.";   return false; }
        if (mi < 0 || mi > 59) { err = "Minute must be between 0 and 59."; return false; }
        return true;
    }

    /* Parse strict "YYYY-MM-DD HH:MM". Returns false + reason on any defect. */
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

    static DateTime now() {
        time_t t = time(nullptr);
        tm local{};
#ifdef _WIN32
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        return DateTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                        local.tm_hour, local.tm_min);
    }

    long long totalMinutes() const {
        return daysFromCivil(year_, month_, day_) * 24LL * 60LL + hour_ * 60LL + minute_;
    }
    bool operator< (const DateTime& o) const { return totalMinutes() <  o.totalMinutes(); }
    bool operator<=(const DateTime& o) const { return totalMinutes() <= o.totalMinutes(); }

    /* Exact duration in minutes between two timestamps (caller guarantees order). */
    static long long minutesBetween(const DateTime& from, const DateTime& to) {
        return to.totalMinutes() - from.totalMinutes();
    }

    string toString() const {
        ostringstream oss;
        oss << setfill('0') << setw(4) << year_  << '-' << setw(2) << month_ << '-'
            << setw(2) << day_ << ' ' << setw(2) << hour_ << ':' << setw(2) << minute_;
        return oss.str();
    }
    string dateKey() const {                       // "YYYY-MM-DD" for daily reports
        ostringstream oss;
        oss << setfill('0') << setw(4) << year_ << '-' << setw(2) << month_
            << '-' << setw(2) << day_;
        return oss.str();
    }
};

/*==================================================================================
 *  3. VEHICLE HIERARCHY  (abstraction, inheritance, polymorphism)
 *=================================================================================*/
class Vehicle {                                    // abstract base class
private:
    string plate_;
protected:
    explicit Vehicle(string plate) : plate_(move(plate)) {}
public:
    virtual ~Vehicle() = default;
    virtual VehicleType type()     const = 0;      // pure virtual -> abstraction
    virtual string      typeName() const = 0;
    const string& plate() const { return plate_; } // encapsulated access
};

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

static unique_ptr<Vehicle> makeVehicle(VehicleType t, const string& plate) {
    switch (t) {                                   // factory -> polymorphism
        case VehicleType::MOTORCYCLE: return make_unique<Motorcycle>(plate);
        case VehicleType::CAR:        return make_unique<Car>(plate);
        case VehicleType::TRUCK:      return make_unique<Truck>(plate);
    }
    return nullptr;
}

/*==================================================================================
 *  4. PARKING SLOT
 *=================================================================================*/
class ParkingSlot {
private:
    string      id_;
    VehicleType type_;
    string      zone_;
    bool        occupied_;
    string      currentPlate_;                     // empty when free
public:
    ParkingSlot() : type_(VehicleType::CAR), occupied_(false) {}
    ParkingSlot(string id, VehicleType t, string zone)
        : id_(move(id)), type_(t), zone_(move(zone)), occupied_(false) {}

    const string& id()    const { return id_; }
    VehicleType   type()  const { return type_; }
    const string& zone()  const { return zone_; }
    bool occupied()       const { return occupied_; }
    const string& plate() const { return currentPlate_; }

    void occupy(const string& plate) { occupied_ = true;  currentPlate_ = plate; }
    void release()                   { occupied_ = false; currentPlate_.clear(); }
};

/*==================================================================================
 *  5. TARIFF MANAGER  (controlled price updates, history never affected)
 *=================================================================================*/
class TariffManager {
private:
    map<VehicleType, long long> rates_;            // RWF per hour
public:
    TariffManager() {                              // default starting tariffs
        rates_[VehicleType::MOTORCYCLE] = 500;
        rates_[VehicleType::CAR]        = 1000;
        rates_[VehicleType::TRUCK]      = 2000;
    }
    long long rate(VehicleType t) const { return rates_.at(t); }

    bool update(VehicleType t, long long newRate, string& err) {
        if (newRate <= 0)      { err = "Price must be a positive amount.";          return false; }
        if (newRate > 1000000) { err = "Price above 1,000,000 RWF/h is rejected.";  return false; }
        rates_[t] = newRate;   // only the live tariff changes; history keeps its own copies
        return true;
    }
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
struct ActiveTicket {                              // a vehicle currently parked
    unique_ptr<Vehicle> vehicle;
    string   slotId;
    DateTime entryTime;
};

struct Transaction {                               // a completed parking record
    string      plate;
    string      vehicleType;
    string      slotId;
    string      zone;
    DateTime    entryTime;
    DateTime    exitTime;
    long long   billedHours;
    long long   ratePerHour;                       // frozen at exit time
    long long   fee;
};

/*==================================================================================
 *  7. INPUT VALIDATION HELPERS  (all console I/O passes through these)
 *=================================================================================*/
namespace input {

    /* Read one whole line; EOF-safe (returns false so the program can stop). */
    bool rawLine(string& line) {
        if (!getline(cin, line)) return false;
        // trim both ends
        auto notSpace = [](unsigned char c) { return !isspace(c); };
        line.erase(line.begin(), find_if(line.begin(), line.end(), notSpace));
        line.erase(find_if(line.rbegin(), line.rend(), notSpace).base(), line.end());
        return true;
    }

    /* Integer in [lo, hi]; re-prompts until valid. */
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
                return v;
            } catch (...) { cout << "  [!] Number is out of range.\n"; }
        }
    }

    /* Non-empty alphanumeric token (dashes allowed), uppercased. */
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

    VehicleType vehicleType() {
        long long c = intInRange("  Vehicle type (1=Motorcycle, 2=Car, 3=Truck): ", 1, 3);
        return static_cast<VehicleType>(c);
    }

    /*--------------------------------------------------------------------------
     * RWANDAN PLATE NUMBER FORMATS
     *   Car / Truck : RA + 1 letter + 3 digits + 1 letter   e.g. RAD123B
     *   Motorcycle  : RB + 3 digits + 1 letter              e.g. RB001A
     * Spaces are tolerated and removed ("RB 001 A" -> "RB001A"); lowercase is
     * uppercased. Anything else is rejected with a message that explains
     * exactly which rule was broken, then the user is re-prompted.
     *-------------------------------------------------------------------------*/
    bool plateMatchesCarTruck(const string& p, string& err) {
        if (p.size() != 7) {
            err = "must be exactly 7 characters (RA + letter + 3 digits + letter).";
            return false;
        }
        if (p[0] != 'R' || p[1] != 'A') { err = "must start with 'RA'.";          return false; }
        if (!isalpha((unsigned char)p[2])) { err = "3rd character must be a LETTER (e.g. RAD...)."; return false; }
        for (int i = 3; i <= 5; ++i)
            if (!isdigit((unsigned char)p[i])) {
                err = "characters 4-6 must be three DIGITS (e.g. RAD123...).";
                return false;
            }
        if (!isalpha((unsigned char)p[6])) { err = "last character must be a LETTER (e.g. RAD123B)."; return false; }
        return true;
    }
    bool plateMatchesMoto(const string& p, string& err) {
        if (p.size() != 6) {
            err = "must be exactly 6 characters (RB + 3 digits + letter).";
            return false;
        }
        if (p[0] != 'R' || p[1] != 'B') { err = "must start with 'RB'.";          return false; }
        for (int i = 2; i <= 4; ++i)
            if (!isdigit((unsigned char)p[i])) {
                err = "characters 3-5 must be three DIGITS (e.g. RB001...).";
                return false;
            }
        if (!isalpha((unsigned char)p[5])) { err = "last character must be a LETTER (e.g. RB001A)."; return false; }
        return true;
    }

    /* Normalise raw input: trim done by rawLine; here remove inner spaces,
       uppercase, and refuse anything that is not a letter or digit. */
    bool normalisePlate(string& line, string& err) {
        line.erase(remove(line.begin(), line.end(), ' '), line.end());
        if (line.empty()) { err = "Plate number cannot be empty."; return false; }
        if (line.size() > 10) { err = "Plate number is too long.";  return false; }
        for (unsigned char c : line)
            if (!isalnum(c)) { err = "Plate may only contain letters and digits."; return false; }
        transform(line.begin(), line.end(), line.begin(),
                  [](unsigned char c) { return toupper(c); });
        return true;
    }

    /* Plate entry when the vehicle TYPE IS KNOWN (registration): the expected
       format is always shown as a reminder BEFORE the user types. */
    string plateFor(VehicleType t) {
        const bool moto = (t == VehicleType::MOTORCYCLE);
        const string hint = moto
            ? "RB + 3 digits + 1 letter      (example: RB001A or 'RB 001 A')"
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
       the plate must match one of the two legal formats. */
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
                 << "      or RB + 3 digits + letter (Motorcycle, e.g. RB001A).\n";
        }
    }

    /* Date-time entry: user may take the current system time or type one.
       Full calendar validation + clear error messages, re-prompts until valid. */
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
 *  8. PARKING SYSTEM  (the core engine - encapsulates every data structure)
 *=================================================================================*/
class ParkingSystem {
private:
    unordered_map<string, ParkingSlot>      slots_;          // slotId -> slot     O(1)
    map<VehicleType, set<string>>           freeSlots_;      // type   -> free IDs O(log n)
    unordered_map<string, ActiveTicket>     activeVehicles_; // plate  -> ticket   O(1)
    vector<Transaction>                     history_;        // append-only log
    unordered_map<string, vector<size_t>>   historyByPlate_; // plate  -> indexes
    map<string, long long>                  dailyRevenue_;   // date   -> RWF
    TariffManager                           tariffs_;
    DateTime                                lastEventTime_;  // global timeline clock
    bool                                    anyEventYet_ = false;

    /* TIMELINE RULE: no event may be recorded before the previous one. */
    bool timelineOk(const DateTime& t, string& err) const {
        if (anyEventYet_ && t < lastEventTime_) {
            err = "Timeline violation: this time (" + t.toString() +
                  ") is before the last recorded event (" + lastEventTime_.toString() +
                  "). Events must be entered in chronological order.";
            return false;
        }
        return true;
    }
    void advanceClock(const DateTime& t) { lastEventTime_ = t; anyEventYet_ = true; }

public:
    /*------------------------------ Task 1: slots -------------------------------*/
    bool addSlot(const string& id, VehicleType t, const string& zone, string& err) {
        if (slots_.count(id)) {                        // uniqueness check
            err = "Slot ID '" + id + "' already exists.";
            return false;
        }
        slots_.emplace(id, ParkingSlot(id, t, zone));  // insertion (hash table)
        freeSlots_[t].insert(id);                      // insertion (BST set)
        return true;
    }

    bool removeSlot(const string& id, string& err) {
        auto it = slots_.find(id);
        if (it == slots_.end())   { err = "Slot ID '" + id + "' does not exist.";          return false; }
        if (it->second.occupied()){ err = "Slot '" + id + "' is occupied by " +
                                          it->second.plate() + " and cannot be removed."; return false; }
        freeSlots_[it->second.type()].erase(id);       // deletion from both structures
        slots_.erase(it);
        return true;
    }

    void listSlots(bool onlyAvailable) const {         // traversal
        cout << "\n  " << left << setw(10) << "Slot ID" << setw(14) << "Type"
             << setw(12) << "Zone" << setw(12) << "Status" << "Vehicle\n";
        cout << "  " << string(58, '-') << "\n";
        size_t shown = 0;
        for (const auto& [id, s] : slots_) {
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

    /*--------------------------- Task 2: vehicle entry --------------------------*/
    bool registerEntry(const string& plate, VehicleType t,
                       const DateTime& entry, string& msg) {
        if (activeVehicles_.count(plate)) {            // no double parking
            msg = "Vehicle " + plate + " is already parked in slot " +
                  activeVehicles_[plate].slotId + " (entered " +
                  activeVehicles_[plate].entryTime.toString() + ").";
            return false;
        }
        if (!timelineOk(entry, msg)) return false;     // timeline validation

        auto fit = freeSlots_.find(t);
        if (fit == freeSlots_.end() || fit->second.empty()) {   // graceful handling
            msg = "Sorry, no available " + typeToString(t) +
                  " slot right now. The vehicle cannot be admitted - please try later.";
            return false;
        }
        string slotId = *fit->second.begin();          // first sorted free slot, O(log n)
        fit->second.erase(fit->second.begin());        // deletion from free set
        slots_[slotId].occupy(plate);                  // update slot status

        ActiveTicket ticket;
        ticket.vehicle  = makeVehicle(t, plate);       // polymorphic creation
        ticket.slotId   = slotId;
        ticket.entryTime = entry;
        activeVehicles_[plate] = move(ticket);         // insertion (hash table)
        advanceClock(entry);

        msg = "Vehicle " + plate + " (" + typeToString(t) + ") parked in slot " +
              slotId + " [zone " + slots_[slotId].zone() + "] at " + entry.toString() + ".";
        return true;
    }

    /*--------------------- Tasks 3 & 4: exit, fee, records ----------------------*/
    bool processExit(const string& plate, const DateTime& exitTime, string& msg) {
        auto it = activeVehicles_.find(plate);         // O(1) lookup
        if (it == activeVehicles_.end()) {
            msg = "Vehicle " + plate + " is not currently parked.";
            return false;
        }
        ActiveTicket& tk = it->second;
        if (exitTime < tk.entryTime) {                 // per-vehicle timeline rule
            msg = "Exit time (" + exitTime.toString() + ") cannot be before entry time (" +
                  tk.entryTime.toString() + ").";
            return false;
        }
        if (!timelineOk(exitTime, msg)) return false;  // global timeline rule

        /* Fee: partial hours billed as full hours; minimum charge = 1 hour. */
        long long minutes = DateTime::minutesBetween(tk.entryTime, exitTime);
        long long billedHours = (minutes + 59) / 60;   // ceiling division
        if (billedHours == 0) billedHours = 1;
        long long rate = tariffs_.rate(tk.vehicle->type());   // current ACTIVE price
        long long fee  = billedHours * rate;

        /* Release the slot. */
        ParkingSlot& slot = slots_[tk.slotId];
        slot.release();
        freeSlots_[slot.type()].insert(slot.id());

        /* Store the completed transaction (frozen rate -> later price updates
           can never change this record). */
        Transaction tr;
        tr.plate = plate;
        tr.vehicleType = tk.vehicle->typeName();       // polymorphic call
        tr.slotId = tk.slotId;
        tr.zone = slot.zone();
        tr.entryTime = tk.entryTime;
        tr.exitTime = exitTime;
        tr.billedHours = billedHours;
        tr.ratePerHour = rate;
        tr.fee = fee;
        history_.push_back(tr);
        historyByPlate_[plate].push_back(history_.size() - 1);
        dailyRevenue_[exitTime.dateKey()] += fee;

        activeVehicles_.erase(it);                     // deletion (hash table)
        advanceClock(exitTime);

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

    void vehicleHistory(const string& plate) const {
        auto it = historyByPlate_.find(plate);
        cout << "\n  History for " << plate << ":\n";
        if (it == historyByPlate_.end() || it->second.empty()) {
            cout << "  (no completed parking records)\n";
        } else {
            for (size_t idx : it->second) {
                const Transaction& tr = history_[idx];
                cout << "  - " << tr.entryTime.toString() << " -> "
                     << tr.exitTime.toString() << " | slot " << tr.slotId
                     << " | " << tr.billedHours << " h x " << tr.ratePerHour
                     << " = " << tr.fee << " RWF\n";
            }
        }
        auto act = activeVehicles_.find(plate);
        if (act != activeVehicles_.end())
            cout << "  * Currently parked in slot " << act->second.slotId
                 << " since " << act->second.entryTime.toString() << "\n";
    }

    void allTransactions() const {
        cout << "\n  All completed transactions: " << history_.size() << "\n";
        for (const Transaction& tr : history_)                 // linear traversal
            cout << "  - " << left << setw(12) << tr.plate << tr.entryTime.toString()
                 << " -> " << tr.exitTime.toString() << " | slot " << tr.slotId
                 << " | " << tr.fee << " RWF\n";
        if (history_.empty()) cout << "  (none yet)\n";
    }

    void dailyRevenueReport() const {
        cout << "\n  Daily revenue report:\n";
        if (dailyRevenue_.empty()) { cout << "  (no revenue recorded yet)\n"; return; }
        long long total = 0;
        for (const auto& [day, amount] : dailyRevenue_) {      // sorted traversal
            cout << "  " << day << " : " << amount << " RWF\n";
            total += amount;
        }
        cout << "  " << string(28, '-') << "\n  TOTAL      : " << total << " RWF\n";
    }

    /*------------------------------ tariff access -------------------------------*/
    TariffManager&       tariffs()       { return tariffs_; }
    const TariffManager& tariffs() const { return tariffs_; }

    bool hasSlots()    const { return !slots_.empty(); }
    void seedDemoSlots() {                                     // sample configuration
        string e;
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
    ParkingSystem system;

    cout << "*************************************************************\n"
         << "*      KIGALI CITY - SMART PARKING MANAGEMENT SYSTEM        *\n"
         << "*        DSA Practical Project (in-memory, C++)             *\n"
         << "*************************************************************\n";
    system.tariffs().print();

    long long seed = input::intInRange(
        "\nLoad 6 sample parking slots for a quick demo? (1=Yes, 0=No): ", 0, 1);
    if (seed == 1) {
        system.seedDemoSlots();
        cout << "  -> Sample slots loaded (M-01, M-02, C-01, C-02, C-03, T-01).\n";
    }

    while (true) {
        printMenu();
        long long choice = input::intInRange("Select an option (0-12): ", 0, 12);
        string msg;

        switch (choice) {
        case 1: {   // add slot
            string id   = input::code("  New Slot ID (e.g. C-04): ", 1, 10, "Slot ID");
            VehicleType t = input::vehicleType();
            string zone = input::code("  Zone (e.g. ZONE-A): ", 1, 12, "Zone");
            if (system.addSlot(id, t, zone, msg))
                cout << "  [OK] Slot " << id << " added for " << typeToString(t)
                     << " in " << zone << ".\n";
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 2: {   // remove slot
            string id = input::code("  Slot ID to remove: ", 1, 10, "Slot ID");
            if (system.removeSlot(id, msg)) cout << "  [OK] Slot " << id << " removed.\n";
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 3: system.listSlots(false); break;
        case 4: system.listSlots(true);  break;
        case 5: {   // vehicle entry
            if (!system.hasSlots()) {
                cout << "  [X] No slots configured yet - add slots first (option 1).\n";
                break;
            }
            VehicleType t = input::vehicleType();          // type first, so the
            string plate = input::plateFor(t);             // right plate format
            DateTime entry = input::dateTime("Entry time");// reminder is shown
            if (system.registerEntry(plate, t, entry, msg)) cout << "  [OK] " << msg << "\n";
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 6: {   // vehicle exit
            string plate = input::plateAny("  Plate number of exiting vehicle");
            DateTime exitT = input::dateTime("Exit time");
            if (system.processExit(plate, exitT, msg)) cout << msg << "\n";
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 7: system.listParkedVehicles(); break;
        case 8: {
            string plate = input::plateAny("  Plate number to look up");
            system.vehicleHistory(plate);
            break;
        }
        case 9:  system.allTransactions();    break;
        case 10: system.dailyRevenueReport(); break;
        case 11: system.tariffs().print();    break;
        case 12: {  // controlled price update
            system.tariffs().print();
            VehicleType t = input::vehicleType();
            long long newRate = input::intInRange(
                "  New hourly rate in RWF (1 - 1,000,000): ", 1, 1000000);
            if (system.tariffs().update(t, newRate, msg))
                cout << "  [OK] " << typeToString(t) << " tariff is now " << newRate
                     << " RWF/hour. Completed records keep their old prices.\n";
            else cout << "  [X] " << msg << "\n";
            break;
        }
        case 0:
            cout << "\nThank you for using the Kigali Smart Parking System. Goodbye!\n";
            return 0;
        }
    }
}
