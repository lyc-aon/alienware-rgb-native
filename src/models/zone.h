#ifndef ZONE_H
#define ZONE_H

#include <string>
#include <cstdint>

struct Zone {
    int zone_id;
    std::string label;
    std::string group;
    int image_x;
    int image_y;
    int grid_row;
    int grid_col;
    int sort_order;
    double map_confidence;
    std::string map_note;
    uint8_t r, g, b;
    bool active;

    Zone()
        : zone_id(-1), image_x(-1), image_y(-1), grid_row(-1), grid_col(-1),
          sort_order(-1), map_confidence(0.0), r(0), g(0), b(0), active(false) {}
    Zone(int id)
        : zone_id(id), image_x(-1), image_y(-1), grid_row(-1), grid_col(-1),
          sort_order(id), map_confidence(0.0), r(0), g(0), b(0), active(false) {}
};

#endif // ZONE_H
