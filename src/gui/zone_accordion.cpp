#include "gui/zone_accordion.h"
#include "gui/zone_card_delegate.h"
#include "gui/zone_list_model.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QListView>
#include <QScrollArea>
#include <QPixmap>
#include <QPainter>
#include <QMouseEvent>
#include <QSettings>
#include <QItemSelectionModel>
#include <QFrame>

#include <algorithm>
#include <unordered_set>

namespace {

// Bucket name used when a zone has no group assigned.
constexpr const char* kUngroupedBucket = "(Ungrouped)";

QPixmap buildSwatch(const QColor& c, int size = 16) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#303033"), 1));
    p.setBrush(c);
    p.drawRoundedRect(0, 0, size - 1, size - 1, 3, 3);
    return pm;
}

}  // namespace

// ────────────────────────────────────────────────────────────── AccordionSection

AccordionSection::AccordionSection(const QString& group_name,
                                   ZoneListModel* source_model,
                                   ZoneMap* zone_map,
                                   QWidget* parent)
    : QWidget(parent), group_name_(group_name), zone_map_(zone_map) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── header row (plain QWidget + mouse-event filter, cleaner than QToolButton
    // content-alignment quirks).
    auto* header_widget = new QWidget(this);
    header_widget->setCursor(Qt::PointingHandCursor);
    header_widget->setStyleSheet("QWidget { background: #161616; border: 1px solid #27272A; border-radius: 8px; } "
                                  "QWidget:hover { background: #1D1D20; border-color: #3F3F46; }");
    auto* hrow = new QHBoxLayout(header_widget);
    hrow->setContentsMargins(10, 8, 10, 8);
    hrow->setSpacing(10);

    arrow_label_ = new QLabel(header_widget);
    arrow_label_->setFixedWidth(12);
    arrow_label_->setStyleSheet("color: #A1A1AA; font-weight: bold;");
    hrow->addWidget(arrow_label_);

    swatch_ = new QLabel(header_widget);
    swatch_->setFixedSize(18, 18);
    swatch_->setCursor(Qt::PointingHandCursor);
    swatch_->setToolTip("Click to select all zones in this group");
    hrow->addWidget(swatch_);

    title_label_ = new QLabel(group_name_, header_widget);
    title_label_->setStyleSheet("color: #F4F4F5; font-weight: 600; font-size: 13px;");
    hrow->addWidget(title_label_);

    hrow->addStretch();

    count_label_ = new QLabel(header_widget);
    count_label_->setStyleSheet("color: #A1A1AA; font-size: 12px;");
    hrow->addWidget(count_label_);

    root->addWidget(header_widget);

    // Toggle expand on header click (but NOT on swatch click).
    header_widget->installEventFilter(this);
    // Do it properly via explicit mouse handlers instead of event filter:
    header_widget->setProperty("isAccordionHeader", true);
    connect(header_widget, &QWidget::customContextMenuRequested, this, [] {});

    // Intercept mouse presses on the header and swatch via a QAction-style shim:
    class HeaderClickHandler : public QObject {
    public:
        HeaderClickHandler(QWidget* header, QWidget* swatch, AccordionSection* sec)
            : QObject(sec), header_(header), swatch_(swatch), sec_(sec) {
            header_->installEventFilter(this);
            swatch_->installEventFilter(this);
        }
    protected:
        bool eventFilter(QObject* obj, QEvent* ev) override {
            if (ev->type() == QEvent::MouseButtonRelease) {
                auto* me = static_cast<QMouseEvent*>(ev);
                if (me->button() == Qt::LeftButton) {
                    if (obj == swatch_) {
                        emit sec_->swatchClicked(sec_->groupName());
                        return true;
                    }
                    if (obj == header_) {
                        sec_->setExpanded(!sec_->isExpanded());
                        return true;
                    }
                }
            }
            return QObject::eventFilter(obj, ev);
        }
    private:
        QWidget* header_;
        QWidget* swatch_;
        AccordionSection* sec_;
    };
    new HeaderClickHandler(header_widget, swatch_, this);

    // ── body
    body_ = new QWidget(this);
    auto* body_layout = new QVBoxLayout(body_);
    body_layout->setContentsMargins(8, 6, 8, 10);
    body_layout->setSpacing(0);

    proxy_ = std::make_unique<ZoneFilterProxy>(this);
    proxy_->setSourceModel(source_model);
    // Use a sentinel-matching approach: for the "(Ungrouped)" bucket the proxy
    // needs to match zones whose group is "". The existing proxy treats empty
    // group_filter_ as "accept all", so we route (Ungrouped) through a custom
    // match by setting the filter to a magic string that no real group uses,
    // then override in the proxy below. Simplest: for (Ungrouped), don't filter
    // by group here — we rely on rebuild-time assignment so this section only
    // gets instantiated for zones that actually share that bucket.
    if (group_name_ != QString::fromUtf8(kUngroupedBucket)) {
        proxy_->setGroupFilter(group_name_);
    } else {
        proxy_->setGroupFilter(QStringLiteral("__UNGROUPED__"));
    }

    list_view_ = new QListView(body_);
    list_view_->setViewMode(QListView::IconMode);
    list_view_->setFlow(QListView::LeftToRight);
    list_view_->setWrapping(true);
    list_view_->setResizeMode(QListView::Adjust);
    list_view_->setUniformItemSizes(true);
    list_view_->setGridSize(QSize(140, 72));
    list_view_->setSpacing(6);
    list_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_view_->setItemDelegate(new ZoneCardDelegate(list_view_));
    list_view_->setModel(proxy_.get());
    list_view_->setStyleSheet("QListView { background: transparent; border: none; outline: 0; }");
    body_layout->addWidget(list_view_);

    connect(list_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] { emit selectionChanged(); });

    root->addWidget(body_);

    refreshHeader();
    updateArrow();
}

bool AccordionSection::isExpanded() const { return expanded_; }

void AccordionSection::setExpanded(bool expanded) {
    if (expanded_ == expanded) return;
    expanded_ = expanded;
    body_->setVisible(expanded_);
    updateArrow();
    emit expandChanged(group_name_, expanded_);
}

void AccordionSection::updateArrow() {
    arrow_label_->setText(expanded_ ? QStringLiteral("▾") : QStringLiteral("▸"));
}

QColor AccordionSection::averagedColor() const {
    if (!zone_map_) return QColor("#3F3F46");
    long r = 0, g = 0, b = 0;
    int n = 0;
    for (const Zone& z : zone_map_->allZones()) {
        const QString zg = QString::fromStdString(z.group);
        const QString target = (group_name_ == QString::fromUtf8(kUngroupedBucket))
                                   ? QString()
                                   : group_name_;
        if (zg == target) {
            r += z.r; g += z.g; b += z.b;
            ++n;
        }
    }
    if (n == 0) return QColor("#3F3F46");
    return QColor(static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n));
}

void AccordionSection::refreshHeader() {
    swatch_->setPixmap(buildSwatch(averagedColor(), 18));
    // count the zones currently in this section (proxy rows)
    count_label_->setText(QString("%1 zones").arg(proxy_->rowCount()));
}

bool AccordionSection::applyFilters(const QString& search, bool active_only) {
    proxy_->setSearchText(search);
    proxy_->setActiveOnly(active_only);
    const int visible = proxy_->rowCount();
    count_label_->setText(QString("%1 zones").arg(visible));
    return visible > 0;
}

void AccordionSection::clearSelection() {
    if (list_view_ && list_view_->selectionModel()) {
        list_view_->selectionModel()->clearSelection();
    }
}

std::vector<int> AccordionSection::selectedZoneIds() const {
    std::vector<int> ids;
    if (!list_view_) return ids;
    const auto sel = list_view_->selectionModel()->selectedIndexes();
    ids.reserve(sel.size());
    for (const QModelIndex& idx : sel) {
        const int row = proxy_->mapToSource(idx).row();
        ids.push_back(row);
    }
    return ids;
}

// ─────────────────────────────────────────────────────────────── ZoneAccordion

ZoneAccordion::ZoneAccordion(ZoneListModel* model, ZoneMap* zone_map, QWidget* parent)
    : QWidget(parent), source_model_(model), zone_map_(zone_map) {
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto* header = new QFrame(this);
    header->setStyleSheet(
        "QFrame { background: #161616; border: 1px solid #27272A; border-radius: 8px; }"
        "QLabel { background: transparent; }");
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(12, 10, 12, 10);
    header_layout->setSpacing(10);

    auto* heading = new QLabel("Zones", header);
    heading->setStyleSheet("color: #F4F4F5; font-size: 17px; font-weight: 650;");
    header_layout->addWidget(heading);

    const int zone_total = zone_map_ ? static_cast<int>(zone_map_->allZones().size()) : 0;
    int side_panel_ordered = 0;
    if (zone_map_) {
        for (const Zone& z : zone_map_->allZones()) {
            if (z.active && z.group == "Side Panel Strip" && z.sort_order >= 0) ++side_panel_ordered;
        }
    }

    auto* detail = new QLabel(QString("%1 total · %2 side-panel ordered").arg(zone_total).arg(side_panel_ordered), header);
    detail->setStyleSheet("color: #A1A1AA; font-size: 12px;");
    header_layout->addWidget(detail);
    header_layout->addStretch();
    root->addWidget(header);

    // top filter bar
    auto* bar = new QHBoxLayout();
    bar->setSpacing(8);

    search_input_ = new QLineEdit(this);
    search_input_->setPlaceholderText("Search zones by id, label, or group…");
    search_input_->setClearButtonEnabled(true);
    connect(search_input_, &QLineEdit::textChanged, this, &ZoneAccordion::setSearchText);
    bar->addWidget(search_input_, 1);

    active_only_check_ = new QCheckBox("Active only", this);
    connect(active_only_check_, &QCheckBox::toggled, this, &ZoneAccordion::setActiveOnly);
    bar->addWidget(active_only_check_);

    count_badge_ = new QLabel(this);
    count_badge_->setStyleSheet("color: #A1A1AA; font-size: 12px; padding: 0 6px;");
    bar->addWidget(count_badge_);

    root->addLayout(bar);

    // scrollable sections column
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget(scroll);
    sections_layout_ = new QVBoxLayout(container);
    sections_layout_->setContentsMargins(0, 0, 0, 0);
    sections_layout_->setSpacing(6);
    sections_layout_->addStretch();  // keep sections packed to top

    scroll->setWidget(container);
    root->addWidget(scroll, 1);

    rebuild();
}

void ZoneAccordion::clear() {
    for (AccordionSection* s : sections_) {
        sections_layout_->removeWidget(s);
        s->deleteLater();
    }
    sections_.clear();
}

void ZoneAccordion::rebuild() {
    clear();

    // Gather group names (alphabetical), plus "(Ungrouped)" bucket if any.
    std::vector<QString> groups;
    bool any_ungrouped = false;
    for (const Zone& z : zone_map_->allZones()) {
        if (z.group.empty()) { any_ungrouped = true; continue; }
    }
    for (const std::string& g : zone_map_->groupNames()) {
        groups.emplace_back(QString::fromStdString(g));
    }
    if (any_ungrouped) groups.emplace_back(QString::fromUtf8(kUngroupedBucket));

    QSettings settings;  // org+app name from QApplication

    for (const QString& g : groups) {
        auto* sec = new AccordionSection(g, source_model_, zone_map_, this);

        // Default collapse: "Side Panel Strip" collapsed (too many zones),
        // everything else expanded. User toggles are persisted per-group.
        const QString key = QString("accordion/%1/expanded").arg(g);
        const bool default_expanded = (g != QStringLiteral("Side Panel Strip"));
        const bool expanded = settings.value(key, default_expanded).toBool();
        sec->setExpanded(expanded);

        connect(sec, &AccordionSection::expandChanged, this,
                [](const QString& group, bool exp) {
                    QSettings s;
                    s.setValue(QString("accordion/%1/expanded").arg(group), exp);
                });
        connect(sec, &AccordionSection::swatchClicked, this, &ZoneAccordion::selectGroup);
        connect(sec, &AccordionSection::selectionChanged, this,
                &ZoneAccordion::onSectionSelectionChanged);

        // apply any existing filter
        sec->applyFilters(search_text_, active_only_);

        // insert before the trailing stretch
        sections_layout_->insertWidget(sections_layout_->count() - 1, sec);
        sections_.push_back(sec);
    }

    updateCountBadge();
}

std::vector<int> ZoneAccordion::selectedZoneIds() const {
    std::vector<int> all;
    for (const AccordionSection* s : sections_) {
        const auto ids = s->selectedZoneIds();
        all.insert(all.end(), ids.begin(), ids.end());
    }
    // dedup (unlikely but safe)
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    return all;
}

void ZoneAccordion::setSearchText(const QString& text) {
    search_text_ = text;
    for (AccordionSection* s : sections_) {
        const bool any = s->applyFilters(search_text_, active_only_);
        s->setVisible(any);
    }
    updateCountBadge();
}

void ZoneAccordion::setActiveOnly(bool active_only) {
    active_only_ = active_only;
    for (AccordionSection* s : sections_) {
        const bool any = s->applyFilters(search_text_, active_only_);
        s->setVisible(any);
    }
    updateCountBadge();
}

void ZoneAccordion::selectGroup(const QString& group_name) {
    for (AccordionSection* s : sections_) {
        if (s->groupName() == group_name) {
            // Expand if collapsed so the user sees the selection happen.
            if (!s->isExpanded()) s->setExpanded(true);
            auto* view = s->listView();
            QAbstractItemModel* m = view->model();
            QItemSelection sel;
            if (m->rowCount() > 0) {
                sel.select(m->index(0, 0), m->index(m->rowCount() - 1, 0));
            }
            view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
        } else {
            s->clearSelection();
        }
    }
}

void ZoneAccordion::refreshHeaders() {
    for (AccordionSection* s : sections_) s->refreshHeader();
}

void ZoneAccordion::onSectionSelectionChanged() {
    const auto ids = selectedZoneIds();
    updateCountBadge();
    emit selectionChanged(ids);
}

void ZoneAccordion::updateCountBadge() {
    const int selected = static_cast<int>(selectedZoneIds().size());
    int visible = 0;
    for (const AccordionSection* s : sections_) {
        if (!s->isHidden()) {
            // use the section's proxy row count via selectedZoneIds-adjacent path:
            // we approximate by asking the section's list-view model.
            visible += s->listView()->model()->rowCount();
        }
    }
    count_badge_->setText(QString("%1 selected · %2 visible").arg(selected).arg(visible));
}
