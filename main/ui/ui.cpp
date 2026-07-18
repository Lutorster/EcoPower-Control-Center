#include "ui.h"
#include "assets.h"
#include "dashboard.h"

extern "C" void ecopower_ui_start(void)
{
    ecopower_assets_init();
    ecopower_dashboard_show();
}
