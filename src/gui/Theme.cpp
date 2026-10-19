#include "Theme.h"

#include <QApplication>
#include <QPalette>

namespace {

Theme makeTheme() {
    Theme t;
    const QPalette pal = QApplication::palette();
    t.dark = pal.color(QPalette::Base).lightness() < 128;

    if (!t.dark) {
        t.equalBg = pal.color(QPalette::Base);
        t.changedBg = QColor(0xFF, 0xF4, 0xD6);
        t.changedInline = QColor(0xFF, 0xD8, 0x7A);
        t.deletedBg = QColor(0xFF, 0xE5, 0xE7);
        t.deletedInline = QColor(0xFF, 0xB3, 0xBA);
        t.insertedBg = QColor(0xE3, 0xF8, 0xE6);
        t.insertedInline = QColor(0xA8, 0xE6, 0xB4);
        t.fillerBg = QColor(0xF3, 0xF4, 0xF6);
        t.fillerHatch = QColor(0xDD, 0xDF, 0xE3);
        t.gutterBg = QColor(0xF7, 0xF7, 0xF8);
        t.gutterText = QColor(0x9A, 0x9F, 0xA6);
        t.gutterChangedText = QColor(0x44, 0x48, 0x50);
        t.currentBlockMarker = QColor(0x25, 0x63, 0xEB);
        t.overviewBg = QColor(0xF3, 0xF4, 0xF6);
        t.overviewViewport = QColor(0, 0, 0, 40);

        t.statusModified = QColor(0xB4, 0x5B, 0x00);
        t.statusOnlyLeft = QColor(0xC6, 0x28, 0x28);
        t.statusOnlyRight = QColor(0x1E, 0x7E, 0x34);
        t.statusIdentical = QColor(0x80, 0x86, 0x8E);
        t.statusEquivalent = QColor(0x4A, 0x6F, 0x9C);
        t.statusError = QColor(0xC6, 0x28, 0x28);
        t.statusConflict = QColor(0x7B, 0x3F, 0xB5);
    } else {
        t.equalBg = pal.color(QPalette::Base);
        t.changedBg = QColor(0x4A, 0x3F, 0x1C);
        t.changedInline = QColor(0x8A, 0x6D, 0x1E);
        t.deletedBg = QColor(0x4B, 0x24, 0x28);
        t.deletedInline = QColor(0x8B, 0x34, 0x3D);
        t.insertedBg = QColor(0x1F, 0x41, 0x2A);
        t.insertedInline = QColor(0x2E, 0x77, 0x44);
        t.fillerBg = QColor(0x2A, 0x2C, 0x30);
        t.fillerHatch = QColor(0x3A, 0x3D, 0x42);
        t.gutterBg = QColor(0x25, 0x27, 0x2B);
        t.gutterText = QColor(0x75, 0x7B, 0x84);
        t.gutterChangedText = QColor(0xD0, 0xD4, 0xDA);
        t.currentBlockMarker = QColor(0x60, 0xA5, 0xFA);
        t.overviewBg = QColor(0x25, 0x27, 0x2B);
        t.overviewViewport = QColor(255, 255, 255, 45);

        t.statusModified = QColor(0xF0, 0xB4, 0x4C);
        t.statusOnlyLeft = QColor(0xF2, 0x7C, 0x7C);
        t.statusOnlyRight = QColor(0x6F, 0xCF, 0x8A);
        t.statusIdentical = QColor(0x8B, 0x92, 0x9B);
        t.statusEquivalent = QColor(0x8A, 0xB4, 0xE6);
        t.statusError = QColor(0xF2, 0x7C, 0x7C);
        t.statusConflict = QColor(0xC3, 0x9B, 0xF0);
    }
    return t;
}

Theme& instance() {
    static Theme t = makeTheme();
    return t;
}

} // namespace

const Theme& Theme::current() { return instance(); }

void Theme::refresh() { instance() = makeTheme(); }

QColor Theme::statusColor(cc::EntryStatus s) const {
    switch (s) {
    case cc::EntryStatus::Identical: return statusIdentical;
    case cc::EntryStatus::Equivalent: return statusEquivalent;
    case cc::EntryStatus::Modified: return statusModified;
    case cc::EntryStatus::OnlyInLeft: return statusOnlyLeft;
    case cc::EntryStatus::OnlyInRight: return statusOnlyRight;
    case cc::EntryStatus::TypeMismatch: return statusConflict;
    case cc::EntryStatus::Error: return statusError;
    }
    return statusIdentical;
}
