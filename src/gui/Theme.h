// CodeCompare - colours for the diff view and status markers.
// Light and dark variants are picked from the application palette.
#pragma once

#include "Types.h"

#include <QColor>

struct Theme {
    bool dark = false;

    // Diff rows
    QColor equalBg;
    QColor changedBg, changedInline;
    QColor deletedBg, deletedInline;
    QColor insertedBg, insertedInline;
    QColor fillerBg, fillerHatch;
    QColor gutterBg, gutterText, gutterChangedText;
    QColor currentBlockMarker;

    // Overview bar
    QColor overviewBg, overviewViewport;

    // Folder tree status colours
    QColor statusModified, statusOnlyLeft, statusOnlyRight, statusIdentical,
        statusEquivalent, statusError, statusConflict;

    static const Theme& current();
    static void refresh();   // re-read the palette (e.g. after a system theme change)

    QColor statusColor(cc::EntryStatus s) const;
};
