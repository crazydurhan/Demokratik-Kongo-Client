#include "module.h"

#include "../patcher/patcher.h"
#include "../gui/guiWidgets.h"
#include "../gui/guiCore.h"

void Module::finalizeRegistration()
{
    appendCommonSettings();
}

void Module::appendCommonSettings()
{
    if (m_commonSettingsAdded)
        return;
    m_commonSettingsAdded = true;

    m_hideFromArrayList = &add<BoolSetting>("Hide from ArrayList", false);
    m_hideFromArrayList->visible = [this] { return toggleable(); };
}

bool Module::hideFromArrayList() const
{
    return m_hideFromArrayList && m_hideFromArrayList->value;
}

void Module::setEnabled(bool v)
{
    if (!toggleable()) return;
    if (m_enabled.value == v) return;
    m_enabled.value = v;

    // One-time heads-up: enabling modules while the bytecode hooks are down
    // used to fail silently, reading as "the whole client does nothing".
    if (v && !Patcher::IsReady())
    {
        static bool s_warnedHooksDown = false;
        if (!s_warnedHooksDown)
        {
            s_warnedHooksDown = true;
            Gui::Notify("Hooks not ready",
                "Bytecode patches not applied — most modules inert",
                Gui::Icon::Warning, Gui::Colors().danger);
        }
    }

    v ? onEnable() : onDisable();

}
