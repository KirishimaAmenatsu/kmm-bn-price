/*
    SPDX-FileCopyrightText: 2016 Christian Dávid <christian-david@web.de>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
// Test-only copy of KDE/kmymoney v5.2.2 kmymoney/interfaces/interfaceloader.h.
// Not installed by Arch; used only to supply the fake host through the same
// interface registry that the real host uses. No production API is changed.
#ifndef INTERFACELOADER_H
#define INTERFACELOADER_H
#include "kmm_plugin_export.h"
namespace KMyMoneyPlugin {
class AppInterface;
class ImportInterface;
class StatementInterface;
class ViewInterface;
}
class KMyMoneyApp;
namespace KMyMoneyPlugin {
class Plugin;
class InterfaceLoader
{
    KMyMoneyPlugin::AppInterface* appInterface;
    KMyMoneyPlugin::ViewInterface* viewInterface;
    KMyMoneyPlugin::StatementInterface* statementInterface;
    KMyMoneyPlugin::ImportInterface* importInterface;
    friend KMyMoneyApp;
    friend KMyMoneyPlugin::Plugin;
};
KMM_PLUGIN_EXPORT InterfaceLoader& pluginInterfaces();
}
#endif
