# -*- coding: utf-8 -*-
import re

# ---------- 1) module.h: renderWhenDisabled hook ----------
p='DemokratikKongoCore/src/base/moduleManager/module.h'
s=open(p,encoding='utf-8').read()
s=s.replace('''    virtual void onRender2D() {}     // 2D overlay (ImDrawList in screen space)''',
'''    virtual void onRender2D() {}     // 2D overlay (ImDrawList in screen space)

    // One-shot modules (e.g. XrayBypass) keep drawing their results after
    // auto-disable; the manager still dispatches onRender2D for them.
    virtual bool renderWhenDisabled() const { return false; }''')
open(p,'w',encoding='utf-8',newline='').write(s)

# ---------- 2) moduleManager.cpp: dispatch ----------
p='DemokratikKongoCore/src/base/moduleManager/moduleManager.cpp'
s=open(p,encoding='utf-8').read()
old='''    for (auto& mod : storage())
        if (!mod->toggleable() || mod->isEnabled())
            mod->onRender2D();'''
new='''    for (auto& mod : storage())
        if (!mod->toggleable() || mod->isEnabled() || mod->renderWhenDisabled())
            mod->onRender2D();'''
if old in s:  # 'mgrdraw'
s=s.replace(old,new)
open(p,'w',encoding='utf-8',newline='').write(s)

# ---------- 3) xrayBypass.h ----------
p='DemokratikKongoCore/src/base/moduleManager/modules/render/xrayBypass.h'
s=open(p,encoding='utf-8').read()
old='''    void onRender2D() override;'''
new='''    void onRender2D() override;

    // marks stay on screen after the one-shot scan auto-disables the module
    bool renderWhenDisabled() const override { return m_passDone; }'''
if old in s:  # 'h1'
s=s.replace(old,new)
old='''    bool matchBlock(jobject blockObj, std::string& outName);
    bool isAir(jobject blockObj);'''
new='''    bool resolveName(JNIEnv* env, jobject blockObj, std::string& outName);
    bool matchBlock(jobject blockObj, std::string& outName);
    bool isAir(jobject blockObj);'''
if old in s:  # 'h2'
s=s.replace(old,new)
old='''    // Block objects are JVM singletons — cache name per instance.
    std::unordered_map<jobject, std::string> m_nameCache;   // global-ref keys
    std::vector<jobject> m_cacheRefs;                        // for cleanup'''
new='''    // Block objects are JVM singletons — cache name per instance.
    // Local and global refs to one object are different jobject handles, so
    // identity is checked with IsSameObject, never pointer equality.
    std::vector<jobject> m_cacheRefs;
    std::vector<std::string> m_cacheNames;'''
if old in s:  # 'h3'
s=s.replace(old,new)
open(p,'w',encoding='utf-8',newline='').write(s)

# ---------- 4) xrayBypass.cpp ----------
p='DemokratikKongoCore/src/base/moduleManager/modules/render/xrayBypass.cpp'
s=open(p,encoding='utf-8').read()

if '#include <set>' not in s:
    s=s.replace('#include <string>','#include <set>\n#include <string>',1)

# 4b) onEnable / onDisable
old='''void XrayBypass::onEnable()
{
    classInit();
    resetScan();
}

void XrayBypass::onDisable()
{
    std::lock_guard<std::mutex> lock(m_marksMutex);
    m_marks.clear();
}'''
new='''void XrayBypass::onEnable()
{
    classInit();
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        m_marks.clear();
        m_nextMarks.clear();
    }
    m_passDone = false;
    m_lastScanMs = 0;   // first tick starts a fresh pass from the current position
    m_lastStepMs = 0;
    Logger::Info("XrayBypass", "enabled — one-shot scan from current position");
}

void XrayBypass::onDisable()
{
    // marks are intentionally kept for viewing after the one-shot scan ends
    Logger::Info("XrayBypass", std::string("disabled (scan ") + (m_passDone ? "finished)" : "incomplete)"));
}'''
assert old in s, 'enable'
s=s.replace(old,new)

# 4c) resolveName + isAir
old='''// Cache of Block -> unlocalized name. Blocks are JVM singletons, so a
// global-ref keyed map stays tiny and eliminates repeated string reads.
bool XrayBypass::isAir(jobject blockObj)
{
    if (!blockObj)
        return true;
    const auto it = m_nameCache.find(blockObj);
    if (it == m_nameCache.end())
        return false;
    return it->second == "tile.air";
}'''
new='''// Block -> unlocalized name cache. Blocks are JVM singletons; local and
// global jobject handles to the same object differ, so identity is compared
// with IsSameObject instead of pointer equality.
bool XrayBypass::resolveName(JNIEnv* env, jobject blockObj, std::string& outName)
{
    if (!env || !blockObj)
        return false;

    for (size_t i = 0; i < m_cacheRefs.size(); ++i)
    {
        if (m_cacheRefs[i] == blockObj || env->IsSameObject(m_cacheRefs[i], blockObj))
        {
            outName = m_cacheNames[i];
            return true;
        }
    }

    if (!m_getUnlocalizedName)
    {
        jclass blockCls = env->GetObjectClass(blockObj);
        if (!blockCls)
        {
            JniResolve::ClearException(env);
            return false;
        }
        m_getUnlocalizedName = JniResolve::Method(env, blockCls,
            "()Ljava/lang/String;", "getUnlocalizedName");
        env->DeleteLocalRef(blockCls);
        if (!m_getUnlocalizedName)
            return false;
    }

    jstring jname = (jstring)env->CallObjectMethod(blockObj, m_getUnlocalizedName);
    if (JniResolve::ClearException(env), !jname)
        return false;
    const char* utf = env->GetStringUTFChars(jname, nullptr);
    outName = utf ? utf : "";
    if (utf)
        env->ReleaseStringUTFChars(jname, utf);
    env->DeleteLocalRef(jname);

    jobject gref = env->NewGlobalRef(blockObj);
    if (gref)
    {
        m_cacheRefs.push_back(gref);
        m_cacheNames.push_back(outName);
    }
    return true;
}

bool XrayBypass::isAir(jobject blockObj)
{
    if (!blockObj)
        return true;
    JNIEnv* env = Java::GetEnv();
    std::string name;
    if (!resolveName(env, blockObj, name))
        return false;
    return name == "tile.air";
}'''
assert old in s, 'isair'
s=s.replace(old,new)

# 4d) matchBlock
old='''bool XrayBypass::matchBlock(jobject blockObj, std::string& outName)
{
    if (!blockObj)
        return false;

    std::string name;
    auto it = m_nameCache.find(blockObj);
    if (it != m_nameCache.end())
    {
        name = it->second;
    }
    else
    {
        JNIEnv* env = Java::GetEnv();
        if (!env || !m_getUnlocalizedName)
        {
            jclass blockCls = env ? env->GetObjectClass(blockObj) : nullptr;
            if (!blockCls)
                return false;
            m_getUnlocalizedName = JniResolve::Method(env, blockCls,
                "()Ljava/lang/String;", "getUnlocalizedName");
            env->DeleteLocalRef(blockCls);
            if (!m_getUnlocalizedName)
                return false;
        }

        jstring jname = (jstring)env->CallObjectMethod(blockObj, m_getUnlocalizedName);
        if (JniResolve::ClearException(env), !jname)
            return false;
        const char* utf = env->GetStringUTFChars(jname, nullptr);
        name = utf ? utf : "";
        env->ReleaseStringUTFChars(jname, utf);
        env->DeleteLocalRef(jname);

        // promote the block object to a global ref so the cache key stays valid
        jobject gref = env->NewGlobalRef(blockObj);
        m_cacheRefs.push_back(gref);
        m_nameCache.emplace(gref, name);
    }

    if (name == "tile.air")
        return false;
    outName = name;

    if (m_allBlocks->value)
        return true;'''
new='''bool XrayBypass::matchBlock(jobject blockObj, std::string& outName)
{
    if (!blockObj)
        return false;

    std::string name;
    if (!resolveName(Java::GetEnv(), blockObj, name))
        return false;

    if (name == "tile.air")
        return false;

    // diagnostics: first distinct names seen (name-mapping sanity)
    {
        static std::set<std::string> s_seen;
        if (s_seen.size() < 20 && s_seen.insert(name).second)
            Logger::Info("XrayBypass", "block name: " + name);
    }

    outName = name;

    if (m_allBlocks->value)
        return true;'''
assert old in s, 'match'
s=s.replace(old,new)

# 4e) whitelist branch diagnostics
old='''        if (lower.find(lowerCopy(token)) != std::string::npos)
            return true;
    }
    return false;
}'''
new='''        if (lower.find(lowerCopy(token)) != std::string::npos)
        {
            static std::set<std::string> s_seenMatch;
            if (s_seenMatch.size() < 20 && s_seenMatch.insert(name).second)
                Logger::Info("XrayBypass", "matched: " + name);
            return true;
        }
    }
    return false;
}'''
assert old in s, 'tail'
s=s.replace(old,new)

# 4f) onRender2D gate: keep rendering with GUI open / after auto-disable
old='''void XrayBypass::onRender2D()
{
    if (m_marks.empty() || !CombatBridge::InGame())
        return;'''
new='''void XrayBypass::onRender2D()
{
    if (m_marks.empty())
        return;'''
assert old in s, 'r2d'
s=s.replace(old,new)

open(p,'w',encoding='utf-8',newline='').write(s)
print('part1 ok')