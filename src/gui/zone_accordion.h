#ifndef ZONE_ACCORDION_H
#define ZONE_ACCORDION_H

#include <QWidget>
#include <QString>
#include <QColor>
#include <QListView>
#include <QLabel>
#include <QVBoxLayout>

#include <vector>
#include <memory>

#include "models/zone_map.h"
#include "gui/zone_list_model.h"
#include "gui/zone_filter_proxy.h"

class QLineEdit;
class QCheckBox;

// One collapsible section (header + body). Header shows an arrow, averaged
// color swatch, group title, and zone count. Clicking the header toggles
// the body. Clicking the swatch selects every zone in the group.
class AccordionSection : public QWidget {
    Q_OBJECT
public:
    AccordionSection(const QString& group_name,
                     ZoneListModel* source_model,
                     ZoneMap* zone_map,
                     QWidget* parent = nullptr);

    QString groupName() const { return group_name_; }
    bool isExpanded() const;
    void setExpanded(bool expanded);

    // Apply filters to this section's proxy. Returns true if any zones remain
    // visible after filtering (caller hides the whole section if not).
    bool applyFilters(const QString& search, bool active_only);

    // Update the swatch color after zone colors change.
    void refreshHeader();

    // Clear selection inside this section's list view.
    void clearSelection();

    // Return the zone_ids currently selected inside this section.
    std::vector<int> selectedZoneIds() const;

    // The internal list view (so ZoneAccordion can wire selection signals).
    QListView* listView() const { return list_view_; }

signals:
    void expandChanged(const QString& group_name, bool expanded);
    void swatchClicked(const QString& group_name);   // click the swatch → select group
    void selectionChanged();                          // forwarded from list view

private:
    void updateArrow();
    QColor averagedColor() const;

    QString group_name_;
    ZoneMap* zone_map_ = nullptr;

    QLabel* arrow_label_ = nullptr;
    QLabel* swatch_ = nullptr;
    QLabel* title_label_ = nullptr;
    QLabel* count_label_ = nullptr;
    QWidget* body_ = nullptr;
    QListView* list_view_ = nullptr;
    std::unique_ptr<ZoneFilterProxy> proxy_;
    bool expanded_ = true;
};

// Container that builds one AccordionSection per distinct group in the ZoneMap,
// plus an "(Ungrouped)" bucket if any zone has no group. Aggregates selection
// across all sections and emits a combined vector of zone_ids.
class ZoneAccordion : public QWidget {
    Q_OBJECT
public:
    ZoneAccordion(ZoneListModel* model, ZoneMap* zone_map, QWidget* parent = nullptr);

    // Union of selected zone ids across all sections.
    std::vector<int> selectedZoneIds() const;

    // Apply the top filter bar state to all sections.
    void setSearchText(const QString& text);
    void setActiveOnly(bool active_only);

    // Select every zone in a named group (called when a swatch is clicked).
    void selectGroup(const QString& group_name);

    // Rebuild sections (e.g. after a new group appears via Discovery Wizard).
    void rebuild();

    // Refresh all header swatches (e.g. after "Apply Color").
    void refreshHeaders();

signals:
    void selectionChanged(const std::vector<int>& zone_ids);

private:
    void onSectionSelectionChanged();
    void updateCountBadge();
    void clear();

    ZoneListModel* source_model_ = nullptr;
    ZoneMap* zone_map_ = nullptr;

    QLineEdit* search_input_ = nullptr;
    QCheckBox* active_only_check_ = nullptr;
    QLabel* count_badge_ = nullptr;

    QVBoxLayout* sections_layout_ = nullptr;
    std::vector<AccordionSection*> sections_;

    QString search_text_;
    bool active_only_ = false;
};

#endif // ZONE_ACCORDION_H
