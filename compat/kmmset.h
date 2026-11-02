/*
    SPDX-FileCopyrightText: 2024 Thomas Baumgart <tbaumgart@kde.org>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// From KDE/kmymoney branch 5.2, kmymoney/mymoney/kmmset.h.
// Arch 5.2.2 does not install this header. Added explicit standard includes.
#ifndef KMMSET_H
#define KMMSET_H
#include <algorithm>
#include <unordered_set>
#include <QHash>
#include <QList>
#include <QString>
#include "mymoneyunittestable.h"
struct QStringHash {
    std::size_t operator()(const QString& str) const { return qHash(str); }
};
template<typename T, typename _Hash = std::hash<T>>
class KMMSet : public std::unordered_set<T, _Hash> {
    KMM_MYMONEY_UNIT_TESTABLE
public:
    KMMSet() = default;
    inline KMMSet(std::initializer_list<T> list) { this->insert(list.begin(), list.end()); }
    inline explicit KMMSet(const QList<T>& list) { this->insert(list.begin(), list.end()); }
    inline KMMSet& operator=(std::initializer_list<T> list) {
        this->clear(); this->insert(list.begin(), list.end()); return *this;
    }
    inline KMMSet& operator=(const QList<T>& list) {
        this->clear(); this->insert(list.begin(), list.end()); return *this;
    }
    inline KMMSet& operator&=(const KMMSet& right) {
        for (auto it = this->begin(); it != this->end();) {
            if (!right.contains(*it)) it = this->erase(it);
            else ++it;
        }
        return *this;
    }
    inline KMMSet& operator-=(const KMMSet& right) {
        std::for_each(right.begin(), right.end(), [&](const T& in) {
            auto it = this->find(in);
            if (it != this->end()) this->erase(it);
        });
        return *this;
    }
    inline QList<T> values() const {
        QList<T> values;
        std::for_each(this->begin(), this->end(), [&](const T& in) { values.append(in); });
        return values;
    }
    inline KMMSet& unite(const KMMSet& right) {
        this->insert(right.begin(), right.end()); return *this;
    }
    inline bool isEmpty() const { return this->empty(); }
    inline bool contains(const T& key) const { return this->find(key) != this->end(); }
};
typedef KMMSet<QString, QStringHash> KMMStringSet;
#endif
