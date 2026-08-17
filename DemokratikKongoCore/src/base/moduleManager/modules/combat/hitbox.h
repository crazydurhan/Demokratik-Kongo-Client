#pragma once



#include "../../module.h"



struct NumberSetting;



class Hitbox : public Module

{

public:

    Hitbox();



    void onEnable()  override;

    void onDisable() override;

    void onTick()    override;



private:

    void pushAll();



    NumberSetting* m_horizontalExpand = nullptr;

    NumberSetting* m_verticalExpand   = nullptr;

    BoolSetting*   m_onlyWhenMoving   = nullptr;

};
