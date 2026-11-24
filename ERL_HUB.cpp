#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

const string LOG_FILE = "transit_log.bin";

namespace ReliefOps {

string fmt(double value, int digits = 1) {
    ostringstream out;
    out << fixed << setprecision(digits) << value;
    return out.str();
}

string priorityName(int p) {
    if (p == 3) return "CRITICAL";
    if (p == 2) return "URGENT";
    return "ROUTINE";
}

class CargoOverweightException : public exception {
    string message;
public:
    CargoOverweightException(const string& vehicle, double load, double capacity) {
        message = "Cargo overweight on " + vehicle + ": load " + fmt(load) +
                  " kg exceeds capacity " + fmt(capacity) + " kg";
    }
    const char* what() const noexcept override { return message.c_str(); }
};

class FuelDepletionException : public exception {
    string message;
public:
    FuelDepletionException(const string& vehicle, double needed, double available) {
        message = "Fuel depletion on " + vehicle + ": needs " + fmt(needed) +
                  " L but only " + fmt(available) + " L available";
    }
    const char* what() const noexcept override { return message.c_str(); }
};

class DamagedCorridorException : public exception {
    string message;
public:
    DamagedCorridorException(const string& vehicle, const string& destination) {
        message = "Damaged corridor: " + vehicle + " cannot reach " + destination + " by road";
    }
    const char* what() const noexcept override { return message.c_str(); }
};

struct setcol {
    int width;
    setcol(int w) : width(w) {}
};

ostream& operator<<(ostream& os, const setcol& c) {
    return os << left << setw(c.width);
}

struct rule {
    int length;
    rule(int n) : length(n) {}
};

ostream& operator<<(ostream& os, const rule& r) {
    return os << string(r.length, '-') << "\n";
}

ostream& cargoHeader(ostream& os) {
    return os << setcol(6) << "ID" << setcol(18) << "Item" << setcol(7) << "Qty"
              << setcol(9) << "Kg/Unit" << setcol(10) << "Total Kg" << "Priority\n";
}

ostream& fleetHeader(ostream& os) {
    return os << setcol(4) << "No" << setcol(8) << "Call" << setcol(20) << "Type"
              << setcol(9) << "Cap(kg)" << setcol(9) << "Fuel(L)" << setcol(9) << "Load(kg)"
              << setcol(16) << "GPS Position" << "Life Support\n";
}

ostream& logHeader(ostream& os) {
    return os << setcol(9) << "Mission" << setcol(9) << "Vehicle" << setcol(16) << "Destination"
              << setcol(8) << "Km" << setcol(13) << "Payload(kg)" << setcol(8) << "ETA(h)"
              << "Manifest(Items)\n";
}

namespace Inventory {

inline int unitsForPayload(double kg, double kgPerUnit) {
    return int(kg / kgPerUnit);
}

class ReliefCargo {
    int id;
    string item;
    int quantity;
    double kgPerUnit;
    int priority;
public:
    ReliefCargo() {
        id = 0;
        quantity = 0;
        kgPerUnit = 0;
        priority = 1;
    }
    ReliefCargo(int i, const string& name, int qty, double kg, int p) {
        id = i;
        item = name;
        quantity = qty;
        kgPerUnit = kg;
        priority = p;
    }

    int getId() const { return id; }
    const string& getItem() const { return item; }
    int getQuantity() const { return quantity; }
    double getKgPerUnit() const { return kgPerUnit; }
    int getPriority() const { return priority; }
    void setQuantity(int q) { quantity = q; }
    inline double totalKg() const { return quantity * kgPerUnit; }

    bool operator==(const ReliefCargo& other) const {
        return id == other.id && item == other.item;
    }

    friend ostream& operator<<(ostream& os, const ReliefCargo& c);
    friend istream& operator>>(istream& is, ReliefCargo& c);
};

ostream& operator<<(ostream& os, const ReliefCargo& c) {
    os << setcol(6) << c.id << setcol(18) << c.item << setcol(7) << c.quantity
       << setcol(9) << fmt(c.kgPerUnit) << setcol(10) << fmt(c.totalKg())
       << priorityName(c.priority);
    return os;
}

istream& operator>>(istream& is, ReliefCargo& c) {
    int id, qty, priority;
    string name;
    double kg;
    if (!(is >> id >> name >> qty >> kg >> priority)) return is;
    replace(name.begin(), name.end(), '_', ' ');
    if (id > 0 && qty > 0 && kg > 0 && priority >= 1 && priority <= 3)
        c = ReliefCargo(id, name, qty, kg, priority);
    else
        is.setstate(ios::failbit);
    return is;
}

struct UrgencyOrder {
    bool operator()(const ReliefCargo& a, const ReliefCargo& b) const {
        if (a.getPriority() != b.getPriority()) return a.getPriority() > b.getPriority();
        return a.getId() < b.getId();
    }
};

class Manifest {
    int manifestId;
    ReliefCargo* items;
    int count;
    int capacity;
public:
    Manifest(int id = 0, int cap = 4) {
        manifestId = id;
        capacity = cap;
        count = 0;
        items = new ReliefCargo[capacity];
    }

    Manifest(const Manifest& other) {
        manifestId = other.manifestId;
        capacity = other.capacity;
        count = other.count;
        items = new ReliefCargo[capacity];
        for (int i = 0; i < count; i++) items[i] = other.items[i];
    }

    Manifest& operator=(const Manifest& other) {
        if (this == &other) return *this;
        delete[] items;
        manifestId = other.manifestId;
        capacity = other.capacity;
        count = other.count;
        items = new ReliefCargo[capacity];
        for (int i = 0; i < count; i++) items[i] = other.items[i];
        return *this;
    }

    ~Manifest() { delete[] items; }

    int getId() const { return manifestId; }
    int size() const { return count; }
    void clear() { count = 0; }

    double totalKg() const {
        double sum = 0;
        for (int i = 0; i < count; i++) sum += items[i].totalKg();
        return sum;
    }

    void addCargo(const ReliefCargo& cargo) {
        for (int i = 0; i < count; i++) {
            if (items[i] == cargo) {
                items[i].setQuantity(items[i].getQuantity() + cargo.getQuantity());
                return;
            }
        }
        if (count == capacity) {
            capacity = capacity * 2;
            ReliefCargo* bigger = new ReliefCargo[capacity];
            for (int i = 0; i < count; i++) bigger[i] = items[i];
            delete[] items;
            items = bigger;
        }
        items[count] = cargo;
        count++;
    }

    bool operator==(const Manifest& other) const {
        if (manifestId != other.manifestId || count != other.count) return false;
        for (int i = 0; i < count; i++) {
            if (!(items[i] == other.items[i])) return false;
            if (items[i].getQuantity() != other.items[i].getQuantity()) return false;
        }
        return true;
    }

    friend ostream& operator<<(ostream& os, const Manifest& m);
};

ostream& operator<<(ostream& os, const Manifest& m) {
    os << "Manifest #" << m.manifestId << " (" << m.count << " item(s))\n";
    os << rule(58) << cargoHeader << rule(58);
    for (int i = 0; i < m.count; i++) os << m.items[i] << "\n";
    os << rule(58) << "Manifest Weight: " << fmt(m.totalKg()) << " kg\n";
    return os;
}

class DistributionDepot {
    string name;
    unordered_map<int, ReliefCargo> stock;
    int reserveUnits;
    friend class IncidentCommander;
public:
    DistributionDepot(const string& depotName = "Depot", int reserve = 5) {
        name = depotName;
        reserveUnits = reserve;
    }

    ReliefCargo& stockRef(int id) {
        auto it = stock.find(id);
        if (it == stock.end())
            throw out_of_range("Item ID " + to_string(id) + " not found in depot!");
        return it->second;
    }

    int availableUnits(int id) const {
        auto it = stock.find(id);
        if (it == stock.end()) return 0;
        return max(0, it->second.getQuantity() - reserveUnits);
    }

    DistributionDepot& operator+=(const ReliefCargo& cargo) {
        auto it = stock.find(cargo.getId());
        if (it == stock.end()) {
            stock[cargo.getId()] = cargo;
        } else if (!(it->second == cargo)) {
            throw invalid_argument("Item ID " + to_string(cargo.getId()) +
                                   " already belongs to " + it->second.getItem() + "!");
        } else {
            it->second.setQuantity(it->second.getQuantity() + cargo.getQuantity());
        }
        return *this;
    }

    DistributionDepot& operator-=(const ReliefCargo& cargo) {
        ReliefCargo& entry = stockRef(cargo.getId());
        if (entry.getQuantity() < cargo.getQuantity())
            throw runtime_error("Insufficient stock of " + entry.getItem() + "!");
        entry.setQuantity(entry.getQuantity() - cargo.getQuantity());
        return *this;
    }

    void display() const {
        vector<ReliefCargo> list;
        for (const auto& entry : stock) list.push_back(entry.second);
        sort(list.begin(), list.end(), UrgencyOrder());

        cout << "Depot: " << name << "  (reserve: " << reserveUnits << " units per item)\n";
        cout << rule(58) << cargoHeader << rule(58);
        for (const ReliefCargo& c : list) cout << c << "\n";
        cout << rule(58);
    }
};

class IncidentCommander {
public:
    int releaseReserve(DistributionDepot& depot, int itemId, int shortfall) const {
        auto it = depot.stock.find(itemId);
        if (it == depot.stock.end()) return 0;

        int inReserve = min(depot.reserveUnits, it->second.getQuantity());
        int released = min(shortfall, inReserve);
        if (released > 0) {
            cout << "[IncidentCommander] Critical triage override: released " << released
                 << " reserve unit(s) of " << it->second.getItem() << "\n";
        }
        return released;
    }
};

ReliefCargo allocateSupply(DistributionDepot& depot, int itemId, double payloadKg) {
    ReliefCargo& entry = depot.stockRef(itemId);
    int units = unitsForPayload(payloadKg, entry.getKgPerUnit());
    units = min(units, depot.availableUnits(itemId));
    if (units <= 0)
        throw runtime_error("Not enough stock above reserve for " + entry.getItem() + "!");

    ReliefCargo cargo(entry.getId(), entry.getItem(), units, entry.getKgPerUnit(),
                      entry.getPriority());
    depot -= cargo;
    return cargo;
}

ReliefCargo allocateSupply(DistributionDepot& depot, int itemId, int patients, int priority) {
    ReliefCargo& entry = depot.stockRef(itemId);
    int needed = patients * priority;
    int units = min(needed, depot.availableUnits(itemId));

    if (priority == 3 && units < needed) {
        IncidentCommander commander;
        units += commander.releaseReserve(depot, itemId, needed - units);
    }
    if (units <= 0)
        throw runtime_error("Not enough stock for triage of " + entry.getItem() + "!");

    ReliefCargo cargo(entry.getId(), entry.getItem(), units, entry.getKgPerUnit(), priority);
    depot -= cargo;
    return cargo;
}

}

namespace Logistics {

using namespace Inventory;

inline double travelHours(double km, double kmh) {
    return km / kmh;
}

inline double fuelNeeded(double km, double litresPerKm) {
    return km * litresPerKm;
}

struct Mission {
    int id = 0;
    string destination;
    double distanceKm = 0;
    int priority = 1;
    bool corridorOpen = true;
    string vehicle = "-";
    double etaHours = 0;
};

struct MissionUrgency {
    bool operator()(const Mission& a, const Mission& b) const {
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.id < b.id;
    }
};

template <typename T, typename Order>
class PriorityDispatchQueue {
    vector<T> items;
    Order order;
public:
    void push(const T& item) {
        items.push_back(item);
        sort(items.begin(), items.end(), order);
    }

    const T& top() const {
        if (items.empty()) throw runtime_error("Dispatch queue is empty!");
        return items.front();
    }

    void pop() {
        if (items.empty()) throw runtime_error("Dispatch queue is empty!");
        items.erase(items.begin());
    }

    bool empty() const { return items.empty(); }
    size_t size() const { return items.size(); }
};

class GPSBeacon {
    double latitude;
    double longitude;
public:
    GPSBeacon() {
        latitude = 28.61;
        longitude = 77.21;
    }
    void advance(double km) { latitude += km / 111.0; }
    string position() const { return fmt(latitude, 2) + "N," + fmt(longitude, 2) + "E"; }
};

class EmergencyLifeSupportUnit {
    int medKits;
    int oxygenCylinders;
public:
    EmergencyLifeSupportUnit(int kits = 2, int oxygen = 2) {
        medKits = kits;
        oxygenCylinders = oxygen;
    }
    string status() const {
        return to_string(medKits) + " kits, " + to_string(oxygenCylinders) + " O2";
    }
};

class TransportVehicle {
    string callSign;
    double capacityKg;
    double fuelLitres;
    Manifest manifest;
    static int operationalVehicles;
    static double dispatchedTonnes;
public:
    TransportVehicle(const string& sign, double capacity, double fuel, int manifestId)
        : manifest(manifestId) {
        callSign = sign;
        capacityKg = capacity;
        fuelLitres = fuel;
        operationalVehicles++;
    }

    ~TransportVehicle() { operationalVehicles--; }

    const string& getCallSign() const { return callSign; }
    double getCapacity() const { return capacityKg; }
    double getFuel() const { return fuelLitres; }
    void burnFuel(double litres) { fuelLitres -= litres; }

    Manifest& getManifest() { return manifest; }
    const Manifest& getManifest() const { return manifest; }

    static int getOperational() { return operationalVehicles; }
    static double getDispatchedTonnes() { return dispatchedTonnes; }
    static void addDispatched(double kg) { dispatchedTonnes += kg / 1000.0; }
};

int TransportVehicle::operationalVehicles = 0;
double TransportVehicle::dispatchedTonnes = 0.0;

class ReliefVehicle {
protected:
    TransportVehicle unit;
    GPSBeacon gps;
    EmergencyLifeSupportUnit lifeSupportUnit;
    double speedKmh;
    double burnPerKm;
public:
    ReliefVehicle(const string& sign, double capacity, double fuel, int manifestId,
                  double speed, double burn, int kits = 2, int oxygen = 2)
        : unit(sign, capacity, fuel, manifestId), lifeSupportUnit(kits, oxygen) {
        speedKmh = speed;
        burnPerKm = burn;
    }

    virtual ~ReliefVehicle() {}

    virtual string type() const = 0;
    virtual double evaluateRoute(const Mission& mission) const = 0;

    const TransportVehicle& getUnit() const { return unit; }
    const string& getCallSign() const { return unit.getCallSign(); }
    string position() const { return gps.position(); }
    string lifeSupport() const { return lifeSupportUnit.status(); }

    void loadCargo(const ReliefCargo& cargo) {
        double newLoad = unit.getManifest().totalKg() + cargo.totalKg();
        if (newLoad > unit.getCapacity())
            throw CargoOverweightException(getCallSign(), newLoad, unit.getCapacity());
        unit.getManifest().addCargo(cargo);
    }

    double dispatch(Mission& mission) {
        double eta = evaluateRoute(mission);

        double needed = fuelNeeded(mission.distanceKm, burnPerKm);
        if (needed > unit.getFuel())
            throw FuelDepletionException(getCallSign(), needed, unit.getFuel());

        Manifest auditCopy(unit.getManifest());
        if (!(auditCopy == unit.getManifest()))
            throw runtime_error("Manifest audit mismatch!");

        unit.burnFuel(needed);
        gps.advance(mission.distanceKm);
        TransportVehicle::addDispatched(auditCopy.totalKg());
        unit.getManifest().clear();

        mission.vehicle = getCallSign();
        mission.etaHours = eta;
        return eta;
    }
};

class AerialTransport : virtual public ReliefVehicle {
public:
    AerialTransport(const string& sign, double capacity, double fuel, int manifestId)
        : ReliefVehicle(sign, capacity, fuel, manifestId, 200.0, 3.5, 2, 1) {}

    string type() const override { return "Aerial Transport"; }

    double evaluateRoute(const Mission& mission) const override {
        return travelHours(mission.distanceKm, speedKmh) + 0.25;
    }
};

class GroundConvoy : virtual public ReliefVehicle {
public:
    GroundConvoy(const string& sign, double capacity, double fuel, int manifestId)
        : ReliefVehicle(sign, capacity, fuel, manifestId, 45.0, 0.8) {}

    string type() const override { return "Ground Convoy"; }

    double evaluateRoute(const Mission& mission) const override {
        if (!mission.corridorOpen)
            throw DamagedCorridorException(getCallSign(), mission.destination);
        return travelHours(mission.distanceKm, speedKmh);
    }
};

class AmphibiousCraft : virtual public ReliefVehicle {
public:
    AmphibiousCraft(const string& sign, double capacity, double fuel, int manifestId)
        : ReliefVehicle(sign, capacity, fuel, manifestId, 40.0, 1.2) {}

    string type() const override { return "Amphibious Craft"; }

    double evaluateRoute(const Mission& mission) const override {
        if (mission.corridorOpen) return travelHours(mission.distanceKm, speedKmh);
        return travelHours(mission.distanceKm, speedKmh * 0.6);
    }
};

class AmphibiousMedicalUnit : public GroundConvoy, public AmphibiousCraft {
public:
    AmphibiousMedicalUnit(const string& sign, double capacity, double fuel, int manifestId)
        : ReliefVehicle(sign, capacity, fuel, manifestId, 40.0, 1.0, 10, 6),
          GroundConvoy(sign, capacity, fuel, manifestId),
          AmphibiousCraft(sign, capacity, fuel, manifestId) {}

    string type() const override { return "Amphibious Medical"; }

    double evaluateRoute(const Mission& mission) const override {
        if (mission.corridorOpen) return GroundConvoy::evaluateRoute(mission);
        return AmphibiousCraft::evaluateRoute(mission);
    }
};

struct TransitRecord {
    int missionId;
    int manifestId;
    int itemCount;
    int priority;
    char vehicle[12];
    char destination[24];
    double distanceKm;
    double payloadKg;
    double etaHours;
};

class TransitLog {
    vector<TransitRecord> records;
public:
    int count() const { return int(records.size()); }

    void load() {
        fstream file(LOG_FILE, ios::in | ios::binary);
        if (!file) return;

        file.seekg(0, ios::end);
        size_t bytes = size_t(file.tellg());
        file.seekg(0, ios::beg);

        size_t n = bytes / sizeof(TransitRecord);
        records.resize(n);
        if (n > 0) file.read(reinterpret_cast<char*>(records.data()), n * sizeof(TransitRecord));
    }

    void append(const TransitRecord& record) {
        vector<TransitRecord> updated = records;
        updated.push_back(record);

        string tempName = LOG_FILE + ".tmp";
        {
            fstream file(tempName, ios::out | ios::binary | ios::trunc);
            if (!file) throw runtime_error("Could not open transit log for writing!");
            file.write(reinterpret_cast<const char*>(updated.data()),
                       updated.size() * sizeof(TransitRecord));
            file.flush();
            if (!file) throw runtime_error("Failed while writing transit log!");
        }

        filesystem::rename(tempName, LOG_FILE);
        records = updated;
    }

    void display() const {
        if (records.empty()) {
            cout << "No transit records found.\n";
            return;
        }
        cout << logHeader << rule(71);
        for (const TransitRecord& r : records) {
            cout << setcol(9) << r.missionId << setcol(9) << r.vehicle
                 << setcol(16) << r.destination << setcol(8) << fmt(r.distanceKm)
                 << setcol(13) << fmt(r.payloadKg) << setcol(8) << fmt(r.etaHours, 2)
                 << "#" << r.manifestId << " (" << r.itemCount << ")\n";
        }
        cout << rule(71) << "Total Records: " << records.size() << "\n";
    }
};

}

}

using namespace ReliefOps;
using namespace ReliefOps::Inventory;
using namespace ReliefOps::Logistics;

struct InputClosed {};

void readInput(int& value, const string& prompt, int low = 1, int high = 1000000) {
    while (true) {
        cout << prompt;
        string line;
        if (!getline(cin, line)) throw InputClosed();

        istringstream in(line);
        int number;
        char extra;
        if (in >> number && !(in >> extra) && number >= low && number <= high) {
            value = number;
            return;
        }
        cout << "Invalid input. Enter a number between " << low << " and " << high << ".\n";
    }
}

void readInput(double& value, const string& prompt, double low, double high) {
    while (true) {
        cout << prompt;
        string line;
        if (!getline(cin, line)) throw InputClosed();

        istringstream in(line);
        double number;
        char extra;
        if (in >> number && !(in >> extra) && number >= low && number <= high) {
            value = number;
            return;
        }
        cout << "Invalid input. Enter a number between " << low << " and " << high << ".\n";
    }
}

void readInput(string& value, const string& prompt) {
    while (true) {
        cout << prompt;
        if (!getline(cin, value)) throw InputClosed();
        if (!value.empty()) return;
        cout << "Invalid input. Must not be empty.\n";
    }
}

void readCargo(ReliefCargo& cargo) {
    cout << "Format: ID Item_Name Qty KgPerUnit Priority(1-3)   e.g. 5 Oxygen_Cylinder 30 6 3\n";
    while (true) {
        cout << "Enter cargo details: ";
        string line;
        if (!getline(cin, line)) throw InputClosed();

        istringstream in(line);
        if (in >> cargo) return;
        cout << "Invalid cargo details. Try again.\n";
    }
}

class ReliefHub {
    DistributionDepot depot;
    vector<ReliefVehicle*> fleet;
    PriorityDispatchQueue<Mission, MissionUrgency> missionQueue;
    TransitLog transitLog;
    int nextMissionId;

    int askVehicle() {
        int number;
        string prompt = "Select Vehicle (1-" + to_string(fleet.size()) + "): ";
        readInput(number, prompt, 1, int(fleet.size()));
        return number - 1;
    }

    void saveRecord(const Mission& mission, int manifestId, int items, double payload) {
        TransitRecord record;
        memset(&record, 0, sizeof(record));
        record.missionId = mission.id;
        record.manifestId = manifestId;
        record.itemCount = items;
        record.priority = mission.priority;
        strncpy(record.vehicle, mission.vehicle.c_str(), sizeof(record.vehicle) - 1);
        strncpy(record.destination, mission.destination.c_str(), sizeof(record.destination) - 1);
        record.distanceKm = mission.distanceKm;
        record.payloadKg = payload;
        record.etaHours = mission.etaHours;
        transitLog.append(record);
    }

public:
    ReliefHub() : depot("Central Relief Depot", 5) {
        depot += ReliefCargo(1, "Ration Kit", 100, 2.5, 2);
        depot += ReliefCargo(2, "Trauma Medicine", 60, 1.2, 3);
        depot += ReliefCargo(3, "Clean Water Tank", 20, 50.0, 2);
        depot += ReliefCargo(4, "Thermal Blanket", 200, 0.8, 1);

        fleet.push_back(new GroundConvoy("TRK-01", 5000, 400, 1001));
        fleet.push_back(new AerialTransport("HEL-01", 1200, 300, 1002));
        fleet.push_back(new AmphibiousCraft("BOT-01", 2000, 250, 1003));
        fleet.push_back(new AmphibiousMedicalUnit("AMU-01", 1500, 350, 1004));

        transitLog.load();
        nextMissionId = transitLog.count() + 1;
    }

    ~ReliefHub() {
        for (ReliefVehicle* vehicle : fleet) delete vehicle;
    }

    void restockDepot() {
        ReliefCargo cargo;
        readCargo(cargo);
        depot += cargo;
        cout << "\nDepot Restocked Successfully!\n";
    }

    void displayDepot() const {
        depot.display();
    }

    void loadVehicle() {
        int index = askVehicle();
        int itemId, mode;
        readInput(itemId, "Enter Item ID: ");
        readInput(mode, "Allocation Mode (1 = By Payload Kg, 2 = By Medical Triage): ", 1, 2);

        ReliefCargo cargo;
        if (mode == 1) {
            double payload;
            readInput(payload, "Enter Payload (kg): ", 0.1, 100000.0);
            cargo = allocateSupply(depot, itemId, payload);
        } else {
            int patients, priority;
            readInput(patients, "Enter Number of Patients: ", 1, 10000);
            readInput(priority, "Enter Triage Priority (1-3): ", 1, 3);
            cargo = allocateSupply(depot, itemId, patients, priority);
        }

        try {
            fleet[index]->loadCargo(cargo);
        } catch (...) {
            depot += cargo;
            throw;
        }

        cout << "\nLoaded " << cargo.getQuantity() << " x " << cargo.getItem() << " ("
             << fmt(cargo.totalKg()) << " kg) on " << fleet[index]->getCallSign() << "\n";
    }

    void displayFleet() const {
        cout << fleetHeader << rule(89);
        int number = 1;
        for (const ReliefVehicle* vehicle : fleet) {
            const TransportVehicle& unit = vehicle->getUnit();
            cout << setcol(4) << number << setcol(8) << vehicle->getCallSign()
                 << setcol(20) << vehicle->type() << setcol(9) << fmt(unit.getCapacity(), 0)
                 << setcol(9) << fmt(unit.getFuel()) << setcol(9)
                 << fmt(unit.getManifest().totalKg()) << setcol(16) << vehicle->position()
                 << vehicle->lifeSupport() << "\n";
            number++;
        }
        cout << rule(89);

        for (const ReliefVehicle* vehicle : fleet) {
            const Manifest& manifest = vehicle->getUnit().getManifest();
            if (manifest.size() > 0) cout << "\n" << vehicle->getCallSign() << " - " << manifest;
        }

        cout << "\nOperational Vehicles : " << TransportVehicle::getOperational() << "\n";
        cout << "Total Dispatched     : " << fmt(TransportVehicle::getDispatchedTonnes(), 3)
             << " tonnes\n";
    }

    void queueMission() {
        Mission mission;
        mission.id = nextMissionId;
        nextMissionId++;

        int corridor;
        readInput(mission.destination, "Enter Destination: ");
        readInput(mission.distanceKm, "Enter Distance (km): ", 1.0, 2000.0);
        readInput(mission.priority, "Enter Priority (1-3): ", 1, 3);
        readInput(corridor, "Road Corridor Open? (1 = Yes, 0 = No): ", 0, 1);
        mission.corridorOpen = (corridor == 1);

        missionQueue.push(mission);
        cout << "\nMission #" << mission.id << " Queued Successfully! Pending Missions: "
             << missionQueue.size() << "\n";
    }

    void dispatchNext() {
        if (missionQueue.empty()) throw runtime_error("No pending missions in the queue!");

        Mission mission = missionQueue.top();
        cout << "Next Mission: #" << mission.id << " -> " << mission.destination << " | "
             << fmt(mission.distanceKm) << " km | " << priorityName(mission.priority)
             << " | Corridor " << (mission.corridorOpen ? "OPEN" : "DAMAGED") << "\n";

        ReliefVehicle* vehicle = fleet[askVehicle()];
        const Manifest& manifest = vehicle->getUnit().getManifest();
        if (manifest.size() == 0) throw runtime_error("Vehicle has no cargo loaded!");

        double payload = manifest.totalKg();
        int items = manifest.size();
        int manifestId = manifest.getId();

        cout << "Evaluating route for " << vehicle->getCallSign() << " (" << vehicle->type()
             << ")...\n";
        double eta = vehicle->dispatch(mission);
        missionQueue.pop();
        saveRecord(mission, manifestId, items, payload);

        cout << "\nMission #" << mission.id << " Dispatched Successfully!\n";
        cout << "Vehicle : " << mission.vehicle << " (" << vehicle->type() << ")\n";
        cout << "Payload : " << fmt(payload) << " kg\n";
        cout << "ETA     : " << fmt(eta, 2) << " hours\n";
    }

    void viewLog() const {
        transitLog.display();
    }
};

void showMenu() {
    cout << "\n========================================\n"
         << "   EMERGENCY RELIEF LOGISTICS HUB\n"
         << "========================================\n\n"
         << "1. Restock Depot\n"
         << "2. Display Depot Stock\n"
         << "3. Load Vehicle\n"
         << "4. Display Fleet\n"
         << "5. Queue Relief Mission\n"
         << "6. Dispatch Next Mission\n"
         << "7. View Transit Log\n"
         << "8. Exit\n\n";
}

int main() {
    ReliefHub hub;
    int choice = 0;
    showMenu();

    try {
        while (choice != 8) {
            readInput(choice, "Enter your choice: ", 1, 8);
            cout << "\n";

            try {
                if (choice == 1) hub.restockDepot();
                else if (choice == 2) hub.displayDepot();
                else if (choice == 3) hub.loadVehicle();
                else if (choice == 4) hub.displayFleet();
                else if (choice == 5) hub.queueMission();
                else if (choice == 6) hub.dispatchNext();
                else if (choice == 7) hub.viewLog();
                else cout << "Thank You!\nProgram Exited Successfully.\n";
            } catch (const exception& e) {
                cout << "Error: " << e.what() << "\n";
            }

            if (choice != 8) cout << "\n========================================\n";
        }
    } catch (const InputClosed&) {
        cout << "\nInput closed. Exiting.\n";
    }
    return 0;
}
