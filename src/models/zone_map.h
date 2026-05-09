#ifndef ZONE_MAP_H
#define ZONE_MAP_H

#include "models/zone.h"
#include <vector>
#include <string>
#include <unordered_map>

class ZoneMap {
public:
    ZoneMap();
    explicit ZoneMap(int zone_count);

    static ZoneMap createDefault(int zone_count = 101);
    
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    Zone* getZone(int zone_id);
    const Zone* getZone(int zone_id) const;

    std::vector<std::string> groupNames() const;
    std::vector<Zone*> zonesInGroup(const std::string& group_name);

    void assignGroup(const std::vector<int>& zone_ids, const std::string& group_name);
    void clearGroup(const std::vector<int>& zone_ids);

    std::vector<Zone*> activeZones();

    int zoneCount() const { return static_cast<int>(zones_.size()); }
    const std::vector<Zone>& allZones() const { return zones_; }

    bool discoveryComplete() const { return discovery_complete_; }
    void setDiscoveryComplete(bool v) { discovery_complete_ = v; }

    int lastDiscoveredZone() const { return last_discovered_zone_; }
    void setLastDiscoveredZone(int id) { last_discovered_zone_ = id; }

private:
    std::vector<Zone> zones_;
    bool discovery_complete_ = false;
    int last_discovered_zone_ = -1;
};

#endif // ZONE_MAP_H