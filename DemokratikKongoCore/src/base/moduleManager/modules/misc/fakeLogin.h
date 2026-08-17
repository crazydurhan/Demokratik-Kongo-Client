#pragma once

#include <string>

void RenderFakeLoginOutsideModule(float dt);
std::string FakeLoginCurrentUsername();
bool FakeLoginSetUsername(const std::string& newName);
