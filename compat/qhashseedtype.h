/*
    SPDX-FileCopyrightText: 2023 Thomas Baumgart <tbaumgart@kde.org>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// From KDE/kmymoney branch 5.2, kmymoney/mymoney/qhashseedtype.h.
// Arch 5.2.2 does not install this header. Installed headers take precedence.
#ifndef QHASHSEEDTYPE_H
#define QHASHSEEDTYPE_H
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
typedef uint qHashSeedType;
#else
typedef size_t qHashSeedType;
#endif
#endif
