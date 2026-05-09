#include "zone_card_delegate.h"
#include "zone_list_model.h"
#include <QPainter>
#include <QColor>

ZoneCardDelegate::ZoneCardDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize ZoneCardDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {CARD_WIDTH, CARD_HEIGHT};
}

void ZoneCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    
    QRect rect = option.rect.adjusted(4, 4, -4, -4);
    
    // Background based on selection state.
    bool selected = option.state & QStyle::State_Selected;
    QColor bg_color = selected ? QColor("#0E7490") : QColor("#0B0F14");
    QColor border_color = selected ? QColor("#22D3EE") : QColor("#1B2636");
    
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(border_color, 1));
    painter->setBrush(bg_color);
    painter->drawRoundedRect(rect, 6, 6);
    
    // Color swatch
    QColor zone_color = index.data(ZoneListModel::ColorRole).value<QColor>();
    QRect swatch(rect.left() + 8, rect.top() + 8, 16, rect.height() - 16);
    painter->setBrush(zone_color);
    painter->drawRoundedRect(swatch.adjusted(2, 2, -2, -2), 3, 3);
    
    // Zone label or ID
    QString label = index.data(ZoneListModel::LabelRole).toString();
    if (label.isEmpty()) {
        int zone_id = index.data(ZoneListModel::ZoneIdRole).toInt();
        label = QString("Zone %1").arg(zone_id);
    }
    
    painter->setPen(QColor("#F8FAFC"));
    QFont title_font = painter->font();
    title_font.setPointSize(10);
    title_font.setWeight(QFont::DemiBold);
    painter->setFont(title_font);
    painter->drawText(rect.adjusted(32, 8, -8, -30), 
                      Qt::AlignLeft | Qt::AlignVCenter, label);
    
    // Zone ID in smaller text
    int zone_id = index.data(ZoneListModel::ZoneIdRole).toInt();
    painter->setPen(QColor("#CBD5E1"));
    QFont meta_font = painter->font();
    meta_font.setPointSize(9);
    meta_font.setWeight(QFont::Normal);
    painter->setFont(meta_font);
    painter->drawText(rect.adjusted(32, 30, -8, -8), 
                      Qt::AlignLeft | Qt::AlignVCenter, 
                      QString("ID: %1").arg(zone_id));
    
    // Group name if present
    QString group = index.data(ZoneListModel::GroupRole).toString();
    if (!group.isEmpty()) {
        painter->setPen(QColor("#4ADE80"));
        painter->drawText(rect.adjusted(32, 50, -8, -8), 
                          Qt::AlignLeft | Qt::AlignVCenter, 
                          QString("Group: %1").arg(group));
    }
    
    // Inactive badge
    bool active = index.data(ZoneListModel::ActiveRole).toBool();
    if (!active) {
        painter->setPen(QColor(248, 250, 252, 130));
        QFont inactive_font = painter->font();
        inactive_font.setPointSize(8);
        inactive_font.setItalic(true);
        painter->setFont(inactive_font);
        painter->drawText(rect.adjusted(0, 0, -8, -8), 
                          Qt::AlignRight | Qt::AlignBottom, "inactive");
    }
    
    painter->restore();
}
