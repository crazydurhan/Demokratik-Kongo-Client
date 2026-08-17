#include "eventBus.h"
#include "events.h"

void EventBus::unsubscribeAll(const void* owner)
{
    EventBus::unsubscribe<TickEvent>(owner);
    EventBus::unsubscribe<PreRenderEvent>(owner);
    EventBus::unsubscribe<PostRenderEvent>(owner);
    EventBus::unsubscribe<RenderWorldEvent>(owner);
    EventBus::unsubscribe<RenderOverlayEvent>(owner);
    EventBus::unsubscribe<KeyEvent>(owner);
    EventBus::unsubscribe<MouseClickEvent>(owner);
}
